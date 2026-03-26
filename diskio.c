#include "drumbox.h"
#include <c64/kernalio.h>

uint8_t g_disk_dev  = 8;
uint8_t g_disk_slot = 0;
char    g_disk_err[40];  /* last drive error string */

static const uint8_t MAGIC[4] = { 'D', 'B', '6', '4' };

static char s_fname_w[12];  /* "DBOXn,S,W\0" */
static char s_fname_r[12];  /* "DBOXn,S,R\0" */
static char s_scr[10];      /* "S0:DBOXn\0" */

static void build_names(uint8_t slot)
{
    char d = (char)('0' + (slot % 10));

    /* Write filename: "DBOXn,S,W" - explicit SEQ write */
    s_fname_w[0]='D'; s_fname_w[1]='B'; s_fname_w[2]='O';
    s_fname_w[3]='X'; s_fname_w[4]=d;
    s_fname_w[5]=','; s_fname_w[6]='S';
    s_fname_w[7]=','; s_fname_w[8]='W';
    s_fname_w[9]=0;

    /* Read filename: "DBOXn,S,R" - explicit SEQ read */
    s_fname_r[0]='D'; s_fname_r[1]='B'; s_fname_r[2]='O';
    s_fname_r[3]='X'; s_fname_r[4]=d;
    s_fname_r[5]=','; s_fname_r[6]='S';
    s_fname_r[7]=','; s_fname_r[8]='R';
    s_fname_r[9]=0;

    /* Scratch: "S0:DBOXn" */
    s_scr[0]='S'; s_scr[1]='0'; s_scr[2]=':';
    s_scr[3]='D'; s_scr[4]='B'; s_scr[5]='O';
    s_scr[6]='X'; s_scr[7]=d; s_scr[8]=0;
}

/* Read drive error channel into g_disk_err[] */
static void read_drive_error(void)
{
    uint8_t i = 0;
    krnio_setnam_n("", 0);
    krnio_open(15, g_disk_dev, 15);
    krnio_chkin(15);
    while (i < 39) {
        char c = krnio_chrin();
        if (krnio_status() & 0x40) break;
        g_disk_err[i++] = c;
    }
    g_disk_err[i] = 0;
    krnio_clrchn();
    krnio_close(15);
}

/* Scratch via command channel */
static void scratch(void)
{
    krnio_setnam(s_scr);
    krnio_open(15, g_disk_dev, 15);
    krnio_close(15);
}

/* ── disk_save_pattern ──────────────────────────────────────────── */
uint8_t disk_save_pattern(uint8_t slot)
{
    uint8_t buf[134];
    uint8_t t, s, i;
    bool ok;

    buf[0]=MAGIC[0]; buf[1]=MAGIC[1]; buf[2]=MAGIC[2]; buf[3]=MAGIC[3];
    buf[4]=g_pattern.kit;
    buf[5]=g_pattern.tempo;
    for (i=0;i<16;i++) buf[6+i]=g_pattern.name[i];
    for (t=0;t<NUM_TRACKS;t++)
        for (s=0;s<NUM_STEPS;s++)
            buf[22+t*NUM_STEPS+s]=g_pattern.steps[t][s];

    build_names(slot);
    scratch();

    krnio_setnam(s_fname_w);
    ok = krnio_open(2, g_disk_dev, 2);
    if (!ok) return 0;

    krnio_chkout(2);
    for (i=0; i<134; i++)
        krnio_chrout(buf[i]);
    krnio_clrchn();
    krnio_close(2);
    read_drive_error();  /* store result in g_disk_err */
    return 1;
}

/* ── disk_load_pattern ──────────────────────────────────────────── */
uint8_t disk_load_pattern(uint8_t slot)
{
    uint8_t buf[134];
    uint8_t i, t, s;
    uint8_t n = 0;

    build_names(slot);

    krnio_setnam(s_fname_r);
    if (!krnio_open(2, g_disk_dev, 2))
        return 0;

    krnio_chkin(2);
    while (n < 134) {
        buf[n++] = krnio_chrin();
        if (krnio_status() & 0x40) break;  /* EOF */
    }
    krnio_clrchn();
    krnio_close(2);

    if (n < 134) return 0;
    if (buf[0]!=MAGIC[0]||buf[1]!=MAGIC[1]||
        buf[2]!=MAGIC[2]||buf[3]!=MAGIC[3]) return 0;

    g_pattern.kit   = buf[4];
    g_pattern.tempo = buf[5];
    if (g_pattern.kit>=NUM_KITS) g_pattern.kit=KIT_909;
    if (g_pattern.tempo<40||g_pattern.tempo>220) g_pattern.tempo=120;
    for (i=0;i<16;i++) g_pattern.name[i]=buf[6+i];
    for (t=0;t<NUM_TRACKS;t++)
        for (s=0;s<NUM_STEPS;s++)
            g_pattern.steps[t][s]=buf[22+t*NUM_STEPS+s]?1:0;

    g_kit=g_pattern.kit;
    seq_set_tempo(g_pattern.tempo);
    return 1;
}