#!/bin/bash
# Copies the files that ship with the port (no game data) into a folder:
#   config.xml ASSETS.ZIP clacon.ttf console02.tga splash.ttf music/soundfont.sf3
# Usage: scripts/stage_bundle.sh <out dir> [config.xml to use]
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$1"
CONFIG="${2:-$REPO/Build_Release/config.xml}"
[ -n "$OUT" ] || { echo "usage: $0 <out dir> [config.xml]"; exit 1; }
mkdir -p "$OUT/music"
cp "$CONFIG" "$OUT/config.xml"
cp "$REPO/Build_Release/clacon.ttf" "$REPO/Build_Release/console02.tga" "$OUT/"
cp "$REPO/ThirdParty/Fonts/CinzelDecorative/CinzelDecorative-Bold.ttf" "$OUT/splash.ttf"
cp "$REPO/Build_Release/music/soundfont.sf3" "$OUT/music/"
rm -f "$OUT/ASSETS.ZIP"
(cd "$REPO/Build_Release/ASSETS" && zip -q -r -X "$(cd "$OUT" && pwd)/ASSETS.ZIP" .)
echo "bundle staged in $OUT"
