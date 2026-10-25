#!/bin/bash
# Windows build (cross-compiled on Linux with MinGW-w64) and package:
#   claw-windows-x86_64.zip = openclaw.exe + SDL2 DLLs + bundled files.
# Usage: platform/windows/build.sh <deps prefix> <build dir> <out dir>
#   deps prefix: made by platform/windows/build-deps.sh
set -e
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
PREFIX="$1"; BUILD="$2"; OUT="$3"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig" PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR=""
cmake -S "$REPO/platform/linux" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$REPO/platform/windows/mingw-toolchain.cmake" -DMINGW_PREFIX="$PREFIX"
cmake --build "$BUILD" -j2
PKG="$OUT/claw-windows-x86_64"
rm -rf "$PKG"; mkdir -p "$PKG"
"$REPO/scripts/stage_bundle.sh" "$PKG"
cp "$BUILD/openclaw.exe" "$PKG/Claw.exe"
x86_64-w64-mingw32-strip "$PKG/Claw.exe" || true
cp "$PREFIX"/bin/*.dll "$PKG/"
cp "$REPO/platform/windows/README.txt" "$PKG/README.txt"
(cd "$OUT" && rm -f claw-windows-x86_64.zip && zip -q -r claw-windows-x86_64.zip claw-windows-x86_64)
echo "$OUT/claw-windows-x86_64.zip"
