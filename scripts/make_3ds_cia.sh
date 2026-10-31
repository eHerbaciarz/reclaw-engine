#!/bin/bash
# reclaw.cia from the 3DS build (after: cmake --build build-3ds).
# Needs makerom (github.com/3DSGuy/Project_CTR) and bannertool on PATH or in
# MAKEROM / BANNERTOOL.
#   scripts/make_3ds_cia.sh [build-3ds]
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
B="${1:-$REPO/build-3ds}"
MAKEROM="${MAKEROM:-makerom}"
BANNERTOOL="${BANNERTOOL:-bannertool}"
C="$REPO/platform/3ds/cia"
"$BANNERTOOL" makebanner -i "$C/banner.png" -a "$C/banner.wav" -o "$B/banner.bnr" > /dev/null
"$MAKEROM" -f cia -o "$B/reclaw.cia" -target t -exefslogo \
    -elf "$B/openclaw.elf" -rsf "$C/reclaw.rsf" \
    -icon "$B/reclaw.smdh" -banner "$B/banner.bnr" -major 0 -minor 1 -micro 0 \
    -DAPP_ROMFS="$B/romfs"
echo "wrote $B/reclaw.cia"
