#!/bin/bash
# Portable Linux package: claw-linux-x86_64.tar.gz
#   openclaw (binary), run.sh, lib/ (SDL2 libraries), bundled files, README.
# The player copies CLAW.REZ (from his own copy of the game) next to run.sh.
# Usage: platform/linux/package.sh <build dir with openclaw> <out dir>
set -e
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$1"; OUT="$2"
[ -x "$BUILD/openclaw" ] || { echo "usage: $0 <build dir> <out dir>"; exit 1; }
PKG="$OUT/claw-linux-x86_64"
rm -rf "$PKG"; mkdir -p "$PKG/lib"
"$REPO/scripts/stage_bundle.sh" "$PKG"
cp "$BUILD/openclaw" "$PKG/openclaw"
strip "$PKG/openclaw" || true
for lib in libSDL2-2.0.so.0 libSDL2_image-2.0.so.0 libSDL2_mixer-2.0.so.0 libSDL2_ttf-2.0.so.0 libSDL2_gfx-1.0.so.0; do
  path=$(ldd "$BUILD/openclaw" | awk -v l="$lib" '$1==l {print $3}')
  [ -n "$path" ] && cp -L "$path" "$PKG/lib/"
done
cat > "$PKG/run.sh" <<'RUN'
#!/bin/sh
# Starts Captain Claw from this folder (SDL2 libraries in lib/).
cd "$(dirname "$0")"
LD_LIBRARY_PATH="$PWD/lib:$LD_LIBRARY_PATH" exec ./openclaw "$@"
RUN
chmod +x "$PKG/run.sh"
cp "$REPO/platform/linux/README.txt" "$PKG/README.txt"
(cd "$OUT" && tar czf claw-linux-x86_64.tar.gz claw-linux-x86_64)
echo "$OUT/claw-linux-x86_64.tar.gz"
