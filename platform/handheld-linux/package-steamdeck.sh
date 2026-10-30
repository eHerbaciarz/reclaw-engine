#!/bin/bash
# Steam Deck package: reclaw-steamdeck.zip
#   ReClaw/ reclaw (binary), ReClaw.sh, bundled files, folder layout, icon, README
# The player copies CLAW.REZ (from his own copy of the game) to data/pl or data/en.
# Usage: platform/handheld-linux/package-steamdeck.sh <build dir> <out dir>
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
BUILD="$1"; OUT="$2"
[ -x "$BUILD/reclaw" ] || { echo "usage: $0 <build dir> <out dir>"; exit 1; }
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
STAGE="$OUT/steamdeck-stage"; PKG="$STAGE/ReClaw"
rm -rf "$STAGE"; mkdir -p "$PKG"
"$REPO/scripts/stage_bundle.sh" "$PKG" "$HERE/config-steamdeck.xml"
"$REPO/scripts/make_reclaw_tree.sh" "$PKG"
cp "$BUILD/reclaw" "$PKG/reclaw"
strip "$PKG/reclaw" 2>/dev/null || true
cp "$REPO/platform/ps4/sce_sys/icon0.png" "$PKG/icon.png"
cat > "$PKG/ReClaw.sh" <<'RUN'
#!/bin/bash
# Starts ReClaw from its folder (add this file to Steam as a non-Steam game).
cd "$(dirname "$(readlink -f "$0")")"
exec ./reclaw "$@"
RUN
chmod +x "$PKG/ReClaw.sh" "$PKG/reclaw"
cp "$HERE/README-steamdeck.txt" "$PKG/README.txt"
rm -f "$OUT/reclaw-steamdeck.zip"
(cd "$STAGE" && zip -qr "$OUT/reclaw-steamdeck.zip" ReClaw)
rm -rf "$STAGE"
echo "$OUT/reclaw-steamdeck.zip"
