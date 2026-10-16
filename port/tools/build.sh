#!/bin/sh
# Builds the port. Usage (Git Bash / MSYS2): port/tools/build.sh [target]
# Toolchains are looked up in <repo>/tools (or PARAPPA_TOOLS): llvm-mingw-*, cmake-*, ninja, SDL3-*.
# PARAPPA_BUILD sets the build directory (default <repo>/build), DECOMP_DIR the Ghidra export
# (default <repo>/analysis/decomp).
cd "$(dirname "$0")/.." || exit 1
ROOT="$(cd .. && pwd)"
TOOLS="${PARAPPA_TOOLS:-$ROOT/tools}"
B="${PARAPPA_BUILD:-$ROOT/build}"
for d in "$TOOLS"/cmake-*/bin "$TOOLS"/ninja*; do [ -d "$d" ] && PATH="$d:$PATH"; done
export PATH PARAPPA_TOOLS="$TOOLS"
[ -f "$B/build.ninja" ] || cmake -S . -B "$B" -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/i686-mingw.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ${DECOMP_DIR:+-DDECOMP_DIR="$DECOMP_DIR"} || exit 1
cmake --build "$B" ${1:+--target "$1"} -- -k 0
