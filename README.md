# PaRappa the Rapper — PC Port

A native Windows port of **PaRappa the Rapper** (PlayStation, US, SCUS-94183), built from a Ghidra
decompilation of the game. It is not an emulator: the game's own code is decompiled to C and compiled
for x86, and Sony's PsyQ libraries are reimplemented in C++ on SDL3 (window, input, audio) and
OpenGL 3.3 (the PS1 GPU).

The whole game plays start to finish: all six stages, the clear movies, the ending, the bonus stage
(KT & the Sunny Funny Band), and memory card saves.

## No game data here

This repository contains **no game data**: no disc files, no executables, no decompilation export.
To build and play you need **your own copy of the game**, dumped as a `.bin` / `.cue` (MODE2/2352).
The build reads code out of your disc through Ghidra, and the port loads everything else from your
disc image at run time.

`port/src/game/fixed/` holds hand-written replacements for a few game functions that Ghidra cannot
decompile correctly, and `port/src/game/patches.json` holds text fixes applied to Ghidra's output.

PaRappa the Rapper is © Sony Interactive Entertainment / NanaOn-Sha. This is an unofficial fan
project and is not affiliated with or endorsed by them.

## Requirements (Windows)

| Tool | Used for |
|---|---|
| [Ghidra](https://github.com/NationalSecurityAgency/ghidra) 12.x | Decompiling the game |
| [ghidra_psx_ldr](https://github.com/lab313ru/ghidra_psx_ldr) built for your Ghidra version | PS-X EXE loader, PsyQ signatures, GTE instructions |
| JDK 21 (e.g. [Temurin](https://adoptium.net/)) | Running Ghidra |
| [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (ucrt, x86_64 host) | Compiling (the port is built 32-bit, i686) |
| [CMake](https://cmake.org/) 3.25+ and [Ninja](https://ninja-build.org/) | Building |
| [SDL3](https://github.com/libsdl-org/SDL/releases) MinGW development package (`SDL3-devel-*-mingw`) | Window, input, audio |
| Python 3 (standard library only) | Disc extraction, decompilation → C |
| Git Bash (or MSYS2) and PowerShell | Build and analysis scripts |

The scripts look for the toolchain in `tools/` by folder name, so unpack them like this (or set the
environment variables shown):

```
tools/
  llvm-mingw-<version>-ucrt-x86_64/   (or LLVM_MINGW)
  cmake-<version>-windows-x86_64/     (or CMake on PATH)
  ninja/ninja.exe                     (or Ninja on PATH)
  SDL3-<version>/                     (or SDL3_DIR = .../i686-w64-mingw32/lib/cmake/SDL3)
  ghidra_<version>_PUBLIC/            (or GHIDRA_INSTALL_DIR)
  jdk-21.<version>/                   (or JAVA_HOME)
```

Install ghidra_psx_ldr into Ghidra (`File > Install Extensions` in the Ghidra window, or unzip it into
`<ghidra>/Ghidra/Extensions`).

## Building

1. **Extract the code from your disc** (the image is only read):

   ```bash
   python scripts/extract_disc.py path/to/PARAPPA.CUE
   ```

   This writes `SCUS_941.83`, `SCUS_text.bin` and the stage overlays `COMODn.BIN` to `game/ghidra_in/`.

2. **Decompile** with Ghidra (headless; roughly 15-20 minutes, needs about 3 GB of free memory):

   ```powershell
   .\scripts\analyze_all.ps1
   ```

   This creates the Ghidra project in `project/` and exports the decompilation to `analysis/decomp/`.
   Java heap size is `GHIDRA_XMX` (default `1536M`). A `NullPointerException` from ghidra_psx_ldr's
   `LibgpuMacroDetector` during analysis is harmless. `.\scripts\ghidra.ps1 gui` opens the project in
   Ghidra; after editing it there, `.\scripts\export_all.ps1` re-exports.

3. **Build** from Git Bash:

   ```bash
   port/tools/build.sh
   ```

   The executable is `build/parappa.exe`, with `SDL3.dll` next to it (`PARAPPA_BUILD` picks another
   build folder).

## Playing

Put your `.bin` and `.cue` next to `parappa.exe` and start it, or pass the image:

```bash
build/parappa.exe --disc path/to/PARAPPA.CUE
```

Without either, it asks for the disc image once and remembers it. Saves go to `memcard\bu00` next to
the exe. Controls, settings, command-line options and test hooks are described in
[port/README.md](port/README.md).

An optional window icon: put a `parappa.ico` in `port/res/` before building.

## Layout

| Path | What |
|---|---|
| `scripts/extract_disc.py` | Pulls the executable and overlays out of your BIN/CUE |
| `scripts/*.ps1`, `scripts/*.java` | Ghidra launch, analysis (PS1 memory map, function seeding, fixups) and C export |
| `port/tools/gen_decomp.py` | Converts the Ghidra export to compilable C (runs during the build) |
| `port/src/game/` | Hand-fixed functions and patches applied on top of Ghidra's output |
| `port/src/psyq/` | Native PsyQ libraries (libgpu, libgs, libgte, libcd, libsnd, libetc, libapi, libc, ...) |
| `port/src/gpu/` | PS1 GPU on OpenGL |
| `port/src/audio/` | SPU voices and CD-XA music |
| `port/src/psx/` | Memory map, disc image, timing, overlay dispatch, crash reports |
| `port/src/host/` | SDL window, input, controls, settings, saves, screenshots |
| `port/tools/*.py` | Audits of the generated C, MIPS→C fallback translator, log symbolizer |

See [port/README.md](port/README.md) for how the port works inside.
