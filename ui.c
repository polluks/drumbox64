#include "drumbox.h"
#include <conio.h>

#define SCR  ((volatile uint8_t *)0x0400)
#define CLR  ((volatile uint8_t *)0xD800)

/* Screen write helpers - sput is raw (already a screen code or digit/punct) */
static void sput(uint8_t row, uint8_t col, uint8_t ch, uint8_t color)
{
    uint16_t o = (uint16_t)row * 40u + col;
    SCR[o] = ch;
    CLR[o] = color;
}

/* sputs is defined later after a2s() - forward declaration not needed in C */

static void sfill(uint8_t row, uint8_t col, uint8_t len, uint8_t ch, uint8_t color)
{
    uint8_t i;
    for (i = 0; i < len; i++)
        sput(row, (uint8_t)(col + i), ch, color);
}

/* Print 3-digit decimal - digits 0-9 are same in both ASCII and screen codes */
static void snum(uint8_t row, uint8_t col, uint8_t v, uint8_t color)
{
    sput(row, col,     (v >= 100) ? (uint8_t)('0' + v / 100) : (uint8_t)' ', color);
    sput(row, col + 1, (uint8_t)('0' + (v % 100) / 10), color);
    sput(row, col + 2, (uint8_t)('0' + v % 10), color);
}

/* Print 2-digit decimal with leading zero */
static void snum2(uint8_t row, uint8_t col, uint8_t v, uint8_t color)
{
    sput(row, col,     (uint8_t)('0' + v / 10), color);
    sput(row, col + 1, (uint8_t)('0' + v % 10), color);
}

/* ── Color palette ───────────────────────────────────────────────── */
#define CW   1   /* white      */
#define CR   2   /* red        */
#define CCY  3   /* cyan       */
#define CYL  7   /* yellow     */
#define CDG 11   /* dark gray  */
#define CLB 14   /* light blue */

/* ── Labels ──────────────────────────────────────────────────────── */
static const char * const TLAB[NUM_TRACKS] = {
    "KICK  ", "SNARE ", "C.HAT ",
    "O.HAT ", "TOM   ", "CLAP  ", "CRASH "
};
static const char * const KLAB[NUM_KITS] = { "909", "808", "ROK" };

/* ── Grid constants ──────────────────────────────────────────────── */
#define GROW  4    /* first grid row on screen */
#define GCOL  7    /* first step column */
#define GSPC  2    /* chars per step */

/* ── Keyboard reading via Oscar64 conio ─────────────────────────── */
/*
 * kbhit() checks if a key is in the buffer (non-blocking).
 * getch() reads one key from the buffer.
 * Both are from Oscar64's <conio.h> and work correctly with the
 * C64 KERNAL keyboard buffer filled by the CIA1 60Hz IRQ.
 *
 * Key codes returned by getch() are PETSCII:
 *   CRSR DOWN=17, UP=145, RIGHT=29, LEFT=157
 *   F1=133, F3=134, F5=135
 *   Space=32, letters=65-90 (uppercase)
 */
uint8_t ui_read_key(void)
{
    if (kbhit())
        return (uint8_t)getch();
    return 0;
}

/* ── Joystick (port 2, $DC00) ────────────────────────────────────── */
/*
 * C64 joystick port 2 is read from CIA1 Port A ($DC00).
 * Bits are ACTIVE LOW: 0 = pressed, 1 = not pressed.
 *   bit 0 = UP     bit 1 = DOWN
 *   bit 2 = LEFT   bit 3 = RIGHT   bit 4 = FIRE
 *
 * We do edge detection (act on press, not hold) plus autorepeat:
 *   - First press: act immediately
 *   - Hold 20 polls: begin repeat every 6 polls
 * This matches typical C64 cursor key feel.
 *
 * Joystick maps to:
 *   UP/DOWN  -> move between tracks
 *   LEFT/RIGHT -> move between steps
 *   FIRE     -> toggle current step
 */
#define JOY2  (*(volatile uint8_t *)0xDC00)

#define JOY_UP    0x01
#define JOY_DOWN  0x02
#define JOY_LEFT  0x04
#define JOY_RIGHT 0x08
#define JOY_FIRE  0x10

/* Autorepeat thresholds (calls to ui_poll_joystick per event) */
#define JOY_DELAY  20    /* frames before autorepeat starts */
#define JOY_RATE    6    /* frames between autorepeat events */

void ui_poll_joystick(void)
{
    static uint8_t s_prev  = 0xFF;  /* previous raw state (all released) */
    static uint8_t s_held  = 0;     /* current held direction (one bit) */
    static uint8_t s_count = 0;     /* hold counter for autorepeat */

    uint8_t raw, pressed, key;

    raw = JOY2 & 0x1F;          /* mask to 5 bits */
    pressed = (~raw) & 0x1F;    /* active-high: 1 = pressed */

    /* Detect newly pressed direction (ignore FIRE for autorepeat) */
    key = 0;

    if (pressed & JOY_UP)    key = 145;   /* CRSR UP    */
    else if (pressed & JOY_DOWN)  key = 17;    /* CRSR DOWN  */
    else if (pressed & JOY_LEFT)  key = 157;   /* CRSR LEFT  */
    else if (pressed & JOY_RIGHT) key = 29;    /* CRSR RIGHT */

    /* FIRE: only on new press, no autorepeat */
    if ((pressed & JOY_FIRE) && !(s_prev & JOY_FIRE)) {
        ui_handle_key(0x20);   /* space = toggle step */
    }

    if (key) {
        if (key != s_held) {
            /* New direction: act immediately, reset counter */
            s_held  = key;
            s_count = 0;
            ui_handle_key(key);
        } else {
            /* Same direction held: autorepeat */
            s_count++;
            if (s_count == JOY_DELAY ||
               (s_count > JOY_DELAY && ((s_count - JOY_DELAY) % JOY_RATE) == 0)) {
                ui_handle_key(key);
            }
        }
    } else {
        s_held  = 0;
        s_count = 0;
    }

    s_prev = pressed;
}

/* ── PETSCII conversion ──────────────────────────────────────────── */
/*
 * The C64 has two character sets selectable via $D018 bit 1:
 *   bit 1 = 0: PETSCII graphics set  (uppercase A-Z = graphic symbols)
 *   bit 1 = 1: PETSCII text set      (uppercase A-Z shown as letters,
 *                                     lowercase a-z = small letters)
 *
 * We use the text set (bit 1 = 1). In this mode, screen codes map as:
 *   'A'-'Z' (65-90)  -> uppercase letters    (screen codes 65-90... NO)
 *
 * Actually screen codes are NOT the same as ASCII even in text mode.
 * The correct mapping for the C64 text charset (upper+lower):
 *   Screen code  1-26  = A-Z  (uppercase)
 *   Screen code 65-90  = a-z  (lowercase, displayed as small letters)
 *   BUT: $D018 bit1=1 swaps: code 65-90 shows UPPERCASE, 1-26 shows lowercase
 *
 * Simplest correct approach: write ASCII 'A'-'Z' and use the charset
 * where those codes display as uppercase. This is achieved by keeping
 * $D018 bit1=0 (default) and using PETSCII screen codes directly:
 *   To show 'A': write screen code 1  (PETSCII $01)
 *   To show 'a': write screen code 1  in the other set
 *
 * CORRECT SIMPLE RULE for default C64 charset ($D018 bit1=0):
 *   Uppercase A-Z: screen code = char - 64   (so 'A'=1, 'B'=2 ... 'Z'=26)
 *   Digits  0-9:  screen code = char - 48+48 = char  (PETSCII digits = ASCII)
 *   Space:        screen code = 32  (same)
 *   '.':          screen code = 46  (same)
 *   '-':          screen code = 45  (same)
 *   '*':          screen code = 42  (same)
 *   '|':          screen code = 93  (PETSCII pipe)
 *   '=':          screen code = 61  (same)
 *   '+':          screen code = 43  (same)
 *   '/':          screen code = 47  (same)
 *   '[':          screen code = 27
 *   ']':          screen code = 29... conflict with cursor right
 *
 * Use ']' screen code 93... actually let's just use a conversion function.
 *
 * ASCII_TO_SCREEN(c): converts ASCII to C64 screen code for uppercase mode.
 */
static uint8_t a2s(uint8_t c)
{
    /* A-Z: screen codes 1-26 (subtract 64) */
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 64u);
    /* a-z: also map to 1-26 (display as uppercase in default charset) */
    if (c >= 'a' && c <= 'z') return (uint8_t)(c - 96u);
    /* Special chars */
    if (c == '|') return 0x5Du;   /* vertical bar PETSCII screen code */
    if (c == '[') return 0x1Bu;
    if (c == ']') return 0x1Du;
    if (c == '@') return 0u;
    /* Space, digits, punct (32-63): same in ASCII and screen codes */
    return c;
}

/* Write a C string with ASCII->screen code conversion */
static void sputs(uint8_t row, uint8_t col, const char *s, uint8_t color)
{
    while (*s && col < 40u)
        sput(row, col++, a2s((uint8_t)*s++), color);
}

/* ── Screen clear ────────────────────────────────────────────────── */
static void sclear(void)
{
    uint16_t i;
    for (i = 0; i < 1000u; i++) {
        SCR[i] = 0x20;   /* space */
        CLR[i] = CDG;
    }
}

/* ── ui_init ─────────────────────────────────────────────────────── */
void ui_init(void)
{
    /* Ensure VIC-II screen is enabled and in correct mode.
     * After crashes/resets, $D011 bit4 (screen enable) may be clear.
     * $D011 = %00011011 = normal text mode, 25 rows, screen on.
     * $D016 = %00001000 = normal 40-col mode, no multicolor. */
    *(volatile uint8_t *)0xD011 = 0x1B;
    *(volatile uint8_t *)0xD016 = 0x08;
    *(volatile uint8_t *)0xD018 = 0x15;   /* screen $0400, charset $D000 (ROM) */
    *(volatile uint8_t *)0xD020 = 0;       /* black border */
    *(volatile uint8_t *)0xD021 = 0;       /* black background */
    sclear();
    ui_draw_full();
}

/* ── ui_draw_full ────────────────────────────────────────────────── */
void ui_draw_full(void)
{
    uint8_t i, col, d;

    /* Row 0: title - more prominent */
    sfill(0, 0, 40, 0x20, 0);   /* black background */
    sputs(0, 2, "** DRUMBOX 64 **", CW);
    sfill(0, 20, 20, 0x20, 0);

    /* Row 3: step header - beat groups colored differently */
    sfill(3, 0, 40, 0x20, 0);
    sputs(3, 0, "TRACK ", CYL);
    sput(3, 6, 0x5D, CDG);
    for (i = 0; i < NUM_STEPS; i++) {
        col = (uint8_t)(GCOL + i * GSPC);
        d   = (i < 9) ? (uint8_t)('1' + i) : (uint8_t)('0' + (i - 9));
        /* Beat 1 = white, beat 2/3/4 = cyan, off-beats = dark */
        sput(3, col, d, ((i & 3) == 0) ? CW : ((i & 1) == 0) ? CCY : CDG);
    }

    /* Row 11: separator - double line feel */
    sfill(11, 0, 40, 0x43, CDG);   /* 0x43 = horizontal line in PETSCII */

    /* Row 12: beat markers */
    sfill(12, 0, 40, 0x20, 0);
    sputs(12, 0, "BEAT:", CLB);
    for (i = 0; i < NUM_STEPS; i++) {
        col = (uint8_t)(GCOL + i * GSPC);
        if ((i & 3) == 0)
            sput(12, col, (uint8_t)('1' + (i >> 2)), CW);
        else if ((i & 1) == 0)
            sput(12, col, '+', CDG);
        else
            sput(12, col, '-', 11);  /* dark gray */
    }

    /* Row 13: separator */
    sfill(13, 0, 40, 0x43, CDG);

    /* Rows 14-20: help with colored key labels
     * Format: [KEY] action  - key in cyan/yellow, action in light blue */
    sfill(14, 0, 40, 0x20, 0);
    sputs(14, 0, "CONTROLS:", CW);

    /* Row 15: movement + toggle */
    sfill(15, 0, 40, 0x20, 0);
    sputs(15,  0, "CRSR", CYL);  sputs(15,  4, ":MOVE  ", CDG);
    sputs(15, 11, "SPC", CYL);   sputs(15, 14, ":TOGGLE ", CDG);
    sputs(15, 23, "JOY", CCY);   sputs(15, 26, ":MOVE+FIRE", CDG);

    /* Row 16: transport */
    sfill(16, 0, 40, 0x20, 0);
    sputs(16,  0, "P", CYL);  sputs(16,  1, ":PLAY/STOP ", CDG);
    sputs(16, 13, "N", CYL);  sputs(16, 14, ":NEXT  ", CDG);
    sputs(16, 22, "B", CYL);  sputs(16, 23, ":PREV", CDG);

    /* Row 17: tempo + pattern */
    sfill(17, 0, 40, 0x20, 0);
    sputs(17,  0, "+/-", CYL); sputs(17,  3, ":TEMPO  ", CDG);
    sputs(17, 11, "C", CYL);   sputs(17, 12, ":CLEAR  ", CDG);
    sputs(17, 20, "R", CYL);   sputs(17, 21, ":RELOAD", CDG);

    /* Row 18: kits + quit */
    sfill(18, 0, 40, 0x20, 0);
    sputs(18,  0, "F1", CYL);  sputs(18,  2, ":909  ", CDG);
    sputs(18,  8, "F3", CYL);  sputs(18, 10, ":808  ", CDG);
    sputs(18, 16, "F5", CYL);  sputs(18, 18, ":ROCK  ", CDG);
    sputs(18, 25, "Q", CR);    sputs(18, 26, ":QUIT", CDG);

    /* Row 19: SID2 */
    sfill(19, 0, 40, 0x20, 0);
    sputs(19,  0, "2", CYL);   sputs(19,  1, ":SID2 ON/OFF  ", CDG);
    sputs(19, 15, "3", CYL);   sputs(19, 16, ":SID2 ADDR", CDG);

    /* Row 20: disk */
    sfill(20, 0, 40, 0x20, 0);
    sputs(20,  0, "W", CCY);   sputs(20,  1, ":SAVE  ", CDG);
    sputs(20,  8, "L", CCY);   sputs(20,  9, ":LOAD  ", CDG);
    sputs(20, 16, "[", CCY);   sputs(20, 17, ":SLT-  ", CDG);
    sputs(20, 24, "]", CCY);   sputs(20, 25, ":SLT+  ", CDG);
    sputs(20, 32, "D", CCY);   sputs(20, 33, ":DRV", CDG);

    /* Row 21: blank */
    sfill(21, 0, 40, 0x20, 0);

    /* Row 22: double separator */
    sfill(22, 0, 40, '=', CLB);

    ui_draw_status();
    ui_draw_grid();
}

/* ── ui_draw_status ──────────────────────────────────────────────── */
void ui_draw_status(void)
{
    uint8_t nb[17];
    uint8_t i;

    /* Row 1: kit / bpm / state / sid */
    sfill(1, 0, 40, 0x20, CLB);
    sputs(1,  0, "KIT:", CLB);
    sputs(1,  4, KLAB[g_kit], CCY);
    sputs(1,  8, "BPM:", CLB);
    snum(1, 12, g_tempo, CCY);
    sputs(1, 17, (g_seq_state == SEQ_PLAYING) ? "** PLAY **" : "- STOP -  ",
                  (g_seq_state == SEQ_PLAYING) ? CYL : CR);
    /* SID2 status with address */
    if (g_dual_sid) {
        /* Show "SID2:DE00" using address label */
        static const char * const albl[] = {"DE00","DF00","D500","D420"};
        sfill(1, 28, 12, 0x20, CDG);
        sputs(1, 28, "SID2:", CCY);
        sputs(1, 33, albl[g_sid2_idx], CCY);
    } else {
        sputs(1, 28, "SID2:OFF    ", CDG);
    }

    /* Row 2: preset */
    sfill(2, 0, 40, 0x20, CLB);
    sputs(2, 0, "PRESET:", CLB);
    snum2(2, 7, g_cur_preset, CW);
    sput(2, 9, '/', CDG);
    snum2(2, 10, g_num_presets, CDG);
    preset_get_name(g_cur_preset, nb);
    for (i = 0; i < 16; i++) sput(2, (uint8_t)(13 + i), a2s(nb[i]), CW);

    /* Row 24: cursor + disk slot + drive */
    sfill(24, 0, 40, 0x20, CLB);
    sputs(24, 0, "TRACK:", CLB);
    sputs(24, 6, TLAB[g_cur_track], CW);
    sputs(24, 13, "ST:", CLB);
    snum2(24, 16, (uint8_t)(g_cur_col + 1), CW);
    sputs(24, 19, "SL:", CLB);
    sput(24, 22, (uint8_t)('0' + g_disk_slot), CW);
    sputs(24, 24, "DRV:", CLB);
    snum2(24, 28, g_disk_dev, CW);
    sputs(24, 31, "W:SAV L:LOD", CDG);
}

/* ── ui_draw_grid ────────────────────────────────────────────────── */
void ui_draw_grid(void)
{
    uint8_t t, s, row, col, val, ch, co;
    uint8_t is_cur, is_play, is_beat1;

    for (t = 0; t < NUM_TRACKS; t++) {
        row = (uint8_t)(GROW + t);

        /* Track label: current=yellow, others=light blue */
        sputs(row, 0, TLAB[t], (t == g_cur_track) ? CYL : CLB);
        sput(row, 6, 0x5D, CDG);

        for (s = 0; s < NUM_STEPS; s++) {
            col     = (uint8_t)(GCOL + s * GSPC);
            val     = g_pattern.steps[t][s];
            is_cur  = (t == g_cur_track && s == g_cur_col)  ? 1 : 0;
            is_play = (g_seq_state == SEQ_PLAYING && s == g_cur_step) ? 1 : 0;
            is_beat1 = ((s & 3) == 0) ? 1 : 0;  /* beat downbeat */

            if (is_cur && is_play) {
                /* Cursor AND playhead: orange */
                ch = val ? 0x51 : 0x1B;
                co = 8;   /* orange */
            } else if (is_cur) {
                /* Cursor position: bright yellow bracket */
                ch = val ? 0x51 : 0x1B;
                co = CYL;
            } else if (is_play) {
                /* Playhead: green for ON, red for OFF */
                ch = val ? 0x51 : 0x2B;   /* 0x2B = '+' for empty playhead */
                co = val ? 5 : 10;         /* 5=green, 10=light red */
            } else if (val) {
                /* Active step: bright on downbeat, normal elsewhere */
                ch = 0x51;   /* filled circle */
                co = is_beat1 ? CW : CYL;
            } else {
                /* Empty step: dim dot, slightly dimmer on off-beats */
                ch = is_beat1 ? 0x2E : 0x2E;
                co = is_beat1 ? CDG : 11;   /* CDG=dark gray, 11=slightly darker */
            }
            sput(row, col, ch, co);
            sput(row, (uint8_t)(col + 1), 0x20, 0);  /* black gap */
        }
    }
}

void ui_draw_playhead(uint8_t step)
{
    (void)step;
    ui_draw_grid();
}

/* ── ui_handle_key ───────────────────────────────────────────────── */
void ui_handle_key(uint8_t key)
{
    uint8_t t, s;

    switch (key) {

    case 17:  /* cursor down */
        if (g_cur_track < NUM_TRACKS - 1) g_cur_track++;
        ui_draw_grid(); ui_draw_status(); break;

    case 145: /* cursor up */
        if (g_cur_track > 0) g_cur_track--;
        ui_draw_grid(); ui_draw_status(); break;

    case 29:  /* cursor right */
        g_cur_col = (g_cur_col < NUM_STEPS - 1) ? g_cur_col + 1 : 0;
        ui_draw_grid(); ui_draw_status(); break;

    case 157: /* cursor left */
        g_cur_col = (g_cur_col > 0) ? g_cur_col - 1 : NUM_STEPS - 1;
        ui_draw_grid(); ui_draw_status(); break;

    case 0x20: /* space: toggle + advance */
        g_pattern.steps[g_cur_track][g_cur_col] ^= 1;
        if (g_cur_col < NUM_STEPS - 1) g_cur_col++;
        ui_draw_grid(); break;

    case 'P': case 'p':
        if (g_seq_state == SEQ_PLAYING) { seq_stop();  ui_draw_grid(); }
        else                            { seq_start(); }
        ui_draw_status(); break;

    case 'S': case 's':
        if (g_seq_state == SEQ_PLAYING) { seq_stop(); ui_draw_grid(); ui_draw_status(); }
        break;

    case '+': case '=':
        if (g_tempo < 220) seq_set_tempo((uint8_t)(g_tempo + 2));
        ui_draw_status(); break;
    case '-':
        if (g_tempo > 40)  seq_set_tempo((uint8_t)(g_tempo - 2));
        ui_draw_status(); break;

    case 'N': case 'n':
        g_cur_preset = (g_cur_preset < (uint8_t)(g_num_presets - 1))
                       ? g_cur_preset + 1 : 0;
        preset_load(g_cur_preset); ui_draw_full(); break;
    case 'B': case 'b':
        g_cur_preset = (g_cur_preset > 0) ? g_cur_preset - 1
                       : (uint8_t)(g_num_presets - 1);
        preset_load(g_cur_preset); ui_draw_full(); break;

    case 'C': case 'c':
        for (t = 0; t < NUM_TRACKS; t++)
            for (s = 0; s < NUM_STEPS; s++)
                g_pattern.steps[t][s] = 0;
        ui_draw_grid(); break;

    case 'R': case 'r':
        preset_load(g_cur_preset); ui_draw_full(); break;

    case 133: g_kit = KIT_909;  ui_draw_status(); break; /* F1 */
    case 134: g_kit = KIT_808;  ui_draw_status(); break; /* F3 */
    case 135: g_kit = KIT_ROCK; ui_draw_status(); break; /* F5 */

    case '2':
        g_dual_sid ^= 1;
        sid_init();
        ui_draw_status(); break;

    case '3':
        /* Cycle SID2 address: DE00 -> DF00 -> D500 -> D420 -> DE00 */
        sid_next_addr();
        ui_draw_status(); break;

    /* ── Disk slot selection ─────────────────────────────── */
    case '[':  /* decrease slot */
        if (g_disk_slot > 0) g_disk_slot--;
        else g_disk_slot = 9;
        ui_draw_status(); break;

    case ']':  /* increase slot */
        if (g_disk_slot < 9) g_disk_slot++;
        else g_disk_slot = 0;
        ui_draw_status(); break;

    case 'D': case 'd':
        /* Cycle drive number 8 -> 9 -> 10 -> 11 -> 12 -> 8 */
        g_disk_dev++;
        if (g_disk_dev > 12) g_disk_dev = 8;
        ui_draw_status(); break;

    /* ── Disk save ───────────────────────────────────────── */
    case 'W': case 'w': {
        uint8_t ok;
        /* Show "SAVING..." in status bar */
        sfill(24, 0, 40, 0x20, CLB);
        sputs(24, 0, "SAVING TO SLOT ", CLB);
        sput(24, 15, (uint8_t)('0' + g_disk_slot), CW);
        sputs(24, 17, "...", CLB);
        ok = disk_save_pattern(g_disk_slot);
        sfill(24, 0, 40, 0x20, CLB);
        if (ok) sputs(24, 0, "SAVED OK       ", CYL);
        else    sputs(24, 0, "SAVE ERR:      ", CR);
        /* Show actual drive error message */
        sputs(24, 9, g_disk_err, ok ? CYL : CR);
        break;
    }

    /* ── Disk load ───────────────────────────────────────── */
    case 'L': case 'l': {
        uint8_t ok;
        sfill(24, 0, 40, 0x20, CLB);
        sputs(24, 0, "LOADING SLOT ", CLB);
        sput(24, 13, (uint8_t)('0' + g_disk_slot), CW);
        sputs(24, 15, "...", CLB);
        ok = disk_load_pattern(g_disk_slot);
        if (ok) {
            ui_draw_full();   /* redraw with new pattern */
        } else {
            sfill(24, 0, 40, 0x20, CLB);
            sputs(24, 0, "LOAD ERR: ", CR);
            sputs(24, 10, g_disk_err, CR);
        }
        break;
    }

    case 'Q': case 'q':
        seq_stop();
        sid_silence();
        seq_restore_irq();
        __asm { jmp $E37B }
        break;

    default: break;
    }
}