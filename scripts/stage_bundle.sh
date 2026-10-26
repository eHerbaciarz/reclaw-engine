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
ZIPOUT="$(cd "$OUT" && pwd)/ASSETS.ZIP"
if command -v zip >/dev/null 2>&1; then
    (cd "$REPO/Build_Release/ASSETS" && zip -q -r -X "$ZIPOUT" .)
else
    python3 - "$REPO/Build_Release/ASSETS" "$ZIPOUT" <<'PY'
import os, sys, zipfile
src, out = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk(src):
        dirs.sort()
        for f in sorted(files):
            p = os.path.join(root, f)
            z.write(p, os.path.relpath(p, src))
PY
fi
echo "bundle staged in $OUT"
