#!/bin/bash
# Windows package: reclaw-windows-x86_64.zip = ReClaw.exe (static, with the
# ReClaw icon) + bundled files + the folder layout. No game data.
# Usage: platform/windows/package.sh <build dir (platform/windows CMake)> <out dir>
set -e
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$1"; OUT="$2"
[ -f "$BUILD/ReClaw.exe" ] || { echo "usage: $0 <build dir> <out dir>"; exit 1; }
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
PKG="$OUT/ReClaw"
rm -rf "$PKG"; mkdir -p "$PKG"
"$REPO/scripts/stage_bundle.sh" "$PKG"
"$REPO/scripts/make_reclaw_tree.sh" "$PKG"
cp "$BUILD/ReClaw.exe" "$PKG/ReClaw.exe"
x86_64-w64-mingw32-strip "$PKG/ReClaw.exe" || true
cp "$REPO/platform/windows/README.txt" "$PKG/README.txt"
(cd "$OUT" && rm -f reclaw-windows-x86_64.zip && zip -q -r reclaw-windows-x86_64.zip ReClaw && rm -rf ReClaw)
echo "$OUT/reclaw-windows-x86_64.zip"
