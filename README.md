# DRUMBOX 64

A 16-step drum machine for the Commodore 64

## Features

- 7 tracks: Kick, Snare, Closed Hat, Open Hat, Tom, Clap, Crash
- 16 steps per track
- 3 drum kits: 909, 808, Rock
- 30 built-in presets
- Dual SID support (4 selectable addresses)
- Save/load patterns to disk (10 slots)
- Joystick support (port 2)

## Build

Requires the [Oscar64 compiler](https://github.com/drmortalwombat/oscar64).

```sh
oscar64 -o=drumbox.prg -O2 \
    main.c sid.c seq.c ui.c presets.c diskio.c -ii=../oscar64/include
```

## Run

```sh
x64sc -autostart drumbox.prg
```

## Controls

| Key | Action |
|-----|--------|
| Cursor keys | Move around the grid |
| Space | Toggle step on/off |
| P | Play / Stop (toggle) |
| S | Stop |
| N / B | Next / Previous preset |
| + / - | Tempo up / down |
| C | Clear pattern |
| R | Reload preset |
| F1 / F3 / F5 | Kit: 909 / 808 / Rock |
| 2 | Toggle dual SID on/off |
| 3 | Cycle SID2 address (DE00 / DF00 / D500 / D420) |
| W | Save pattern to disk slot |
| L | Load pattern from disk slot |
| [ / ] | Previous / Next disk slot (0-9) |
| D | Cycle drive number (8-12) |
| Q | Quit to BASIC |

Joystick (port 2): Up/Down = change track, Left/Right = change step, Fire = toggle step.

## Disk I/O

Patterns are saved as sequential files named `DBOX0` through `DBOX9`.
Select a slot with `[` / `]`, select a drive with `D`, then press `W` to save or `L` to load.
Should work with 1541, 1571, 1581, and SD2IEC drives.
