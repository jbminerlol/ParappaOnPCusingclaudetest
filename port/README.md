# PaRappa the Rapper — PC port

A native Windows port of PaRappa the Rapper (PS1, SCUS-94183) built from a Ghidra decompilation of
the game. The game code runs as decompiled C; Sony's PsyQ libraries are reimplemented in C++ on
SDL3 (window, input, audio) and OpenGL 3.3 (the PS1 GPU).

You need your own copy of the game disc (`PARAPPA.BIN` / `.CUE`, MODE2/2352).

## How it fits together

| Piece | Where |
|---|---|
| Ghidra project + analysis scripts | `../project`, `../scripts` (`analyze_all.ps1`, `export_all.ps1`) |
| Decompilation export (C + function/global tables) | `../analysis/decomp` |
| Ghidra output → compilable C | `tools/gen_decomp.py` (runs during the build) |
| Hand-fixed functions (replace Ghidra output) | `src/game/fixed/<name>.c` |
| Text patches on Ghidra output | `src/game/patches.json` |
| Fallback MIPS→C translation for undecompilable functions | `tools/mips2c.py` |
| Call-site argument checker | `tools/check_calls.py` |
| Native PsyQ libraries | `src/psyq` (libgpu, libgs, libgte, libcd + St, libpress, libsnd, libetc, libapi, libc) |
| PS1 GPU on OpenGL | `src/gpu` |
| SPU voices / CD-XA audio | `src/audio` |
| Memory map, disc image, timing, dispatch | `src/psx` |

The port is a 32-bit large-address-aware executable that maps PS1 main RAM at `0x80000000` and the
scratchpad at `0x1F800000`, so the game's pointers and data structures are used unchanged.
Function pointers are kept as MIPS addresses and resolved by `psx_resolve()`; stage overlays
(`COMODn.BIN`, all loaded at `0x801C3870`) are told apart by their code bytes.

## Building

The build needs the decompilation export in `../analysis/decomp` (see the top-level README) and the
toolchain in `../tools` (llvm-mingw, CMake, Ninja, SDL3; `PARAPPA_TOOLS` points elsewhere). From
Git Bash:

```bash
port/tools/build.sh
```

The build directory is `../build` (`PARAPPA_BUILD` overrides it). Security software that blocks
unsigned programs from writing to some folders (Trend Micro Folder Shield) can stop the compiler;
then build somewhere it allows, e.g. `PARAPPA_BUILD="$LOCALAPPDATA/parappa-build"`.

After changing analysis in Ghidra (by hand, through GhidraMCP or in `scripts/PsxFixups.java`),
re-export with `scripts/export_all.ps1`; the next build regenerates the C.

## Running

```bash
build/parappa.exe --disc "path/to/PARAPPA.CUE"
```

Without `--disc` the port uses a PaRappa image next to the executable, else the one chosen last
time, else it asks for one (`--pick-disc` asks again). Options: `--scale N` (internal resolution,
default 2), `--window N` (window size multiplier, default 3), `--controls FILE` (key bindings),
`--debug` (verbose log).

`parappa.log`, `settings.ini`, `controls.ini` and screenshots (F12) go to `%APPDATA%\PaRappaPort\`.

### Saving

The game's own memory card saves work as on the console: answer YES at "Save?" after a stage,
and use LOAD in the MENU screen. Memory card slot 1 is the folder `memcard\bu00` next to
`parappa.exe` (one file per save, 15 blocks in all); slot 2 is empty. Clearing all six stages with
COOL unlocks the bonus stage, KT & the Sunny Funny Band, as the seventh entry of STAGE SELECT.

Security software can stop programs from creating files in some folders (Trend Micro Folder Shield
kills them). The port checks the game folder once, with a short-lived copy of itself; if that copy
is stopped, the saves go to `%APPDATA%\PaRappaPort\memcard\bu00` instead, and the game says so once
in a message box. To check again after allowing `parappa.exe` in the security software, delete the
`saves_checked_for` line from `settings.ini`: the saves then go next to the exe, and the ones made
so far are copied there. `Open Saves Folder.bat` (copied next to the exe by the build) opens
whichever folder holds the saves.

### Settings

`settings.ini` holds the disc image path (`disc`) and `audio_offset_ms`. The rhythm clock follows
the music actually coming out of the speakers (the port measures its audio buffering and the
sound device's buffer), so presses are judged as on a PS1 with a TV. If presses still feel late,
for example with wireless headphones, set `audio_offset_ms` to the extra delay (try 50-150).

### Controls

A DualShock 4 or DualSense plays like the original controller: every button maps to the same
button (USB or Bluetooth, no extra drivers). The pad's light bar and player LEDs show its controller
port. Other gamepads (Xbox and so on) map by position. A second pad is controller port 2; if the
first one is unplugged, the other takes over port 1.

| PS1 | DualShock 4 / DualSense | Xbox-style pad | Keyboard |
|---|---|---|---|
| D-pad | D-pad or left stick | D-pad or left stick | Arrow keys |
| △ ○ × □ | △ ○ × □ | Y B A X | W D S A |
| L1 R1 / L2 R2 | L1 R1 / L2 R2 | LB RB / LT RT | Q E / 1 3 |
| Start | Options | Menu | Enter |
| Select | Share / Create or touchpad click | View | Right Shift, Backspace |

Bindings live in `controls.ini`, created with these defaults on first run in
`%APPDATA%\PaRappaPort\` (or pass `--controls <file>`). Each PS1 button takes a comma-separated
list of SDL key names (`[keyboard]`) and gamepad inputs (`[gamepad]`: `cross`, `circle`,
`square`, `triangle`, `l1`...`r3`, `options`, `share`, `touchpad`, `dpup`, `leftx-`, ... or
SDL names); delete the file to restore the defaults. A `controls.ini` written by an older version
of the port is replaced with the new defaults (the old one is kept as `controls.old.ini`).

If Steam or DS4Windows is running, it may turn the pad into a virtual Xbox controller; that still
works, mapped by position.

F11 toggles fullscreen, F12 saves a screenshot.

## Test hooks

Environment variables for scripted runs (with `--debug`, the log also records rank changes and the
rhythm clock's lag behind the drive):

| Variable | Effect |
|---|---|
| `PARAPPA_AUTOINPUT=3400:start,4700:cross` | Presses buttons at those vblanks; the real keyboard and gamepads are ignored |
| `PARAPPA_SCREENSHOTS=600,900` | Saves `shot_<vblank>.png` at those vblanks |
| `PARAPPA_TEST_RANK=0..3` | Holds the rank (COOL, GOOD, BAD, AWFUL) during stages |
| `PARAPPA_TEST_UNLOCK=1` | Marks stages 1-6 cleared with COOL (unlocks the bonus stage) |
| `PARAPPA_DUMP_REPLAY=1` | Writes the attract demo's recorded presses to `replay_<overlay>.txt` |
| `PARAPPA_AUTOPLAY=<replay file>` | Presses those buttons on time through the controller path (with `PARAPPA_AUTOINPUT` for the menus) |
| `PARAPPA_WATCH=<address>` | Logs every write to a PS1 address |

The real game start from boot is `PARAPPA_AUTOINPUT="3400:start,4700:start,5000:circle,5300:cross"`
(starting earlier lands in the attract demo).
