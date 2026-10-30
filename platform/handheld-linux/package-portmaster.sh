#!/bin/bash
# PortMaster package: reclaw-portmaster.zip
#   ReClaw.sh            launcher (goes to roms/ports/)
#   reclaw/              the game folder: reclaw.aarch64, bundled files,
#                        folder layout, port.json, gameinfo.xml, README.md
# The player copies CLAW.REZ (from his own copy of the game) to
# ports/reclaw/data/pl or data/en.
# Usage: platform/handheld-linux/package-portmaster.sh <build dir> <out dir>
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
BUILD="$1"; OUT="$2"
[ -x "$BUILD/reclaw" ] || { echo "usage: $0 <build dir> <out dir>"; exit 1; }
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
STAGE="$OUT/portmaster-stage"; PKG="$STAGE/reclaw"
rm -rf "$STAGE"; mkdir -p "$PKG"
"$REPO/scripts/stage_bundle.sh" "$PKG" "$HERE/config-portmaster.xml"
"$REPO/scripts/make_reclaw_tree.sh" "$PKG"
cp "$BUILD/reclaw" "$PKG/reclaw.aarch64"
llvm-strip-18 "$PKG/reclaw.aarch64" 2>/dev/null || llvm-strip "$PKG/reclaw.aarch64" 2>/dev/null || true
chmod +x "$PKG/reclaw.aarch64"
python3 - "$REPO/platform/ps4/sce_sys/pic1.png" "$PKG/screenshot.png" <<'PY'
import sys
from PIL import Image
Image.open(sys.argv[1]).convert("RGB").resize((640, 360), Image.LANCZOS).save(sys.argv[2])
PY

cat > "$STAGE/ReClaw.sh" <<'RUN'
#!/bin/bash
# ReClaw - an engine for Claw (1997). Needs CLAW.REZ from your copy of the game
# in ports/reclaw/data/pl/ (Polish release) or data/en/ (English release).

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/reclaw"
cd "$GAMEDIR"
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

export LD_LIBRARY_PATH="$GAMEDIR/libs.${DEVICE_ARCH}:$LD_LIBRARY_PATH"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

# Select + Start quits (gptokeyb), the game itself reads the gamepad.
$GPTOKEYB "reclaw.${DEVICE_ARCH}" &
pm_platform_helper "$GAMEDIR/reclaw.${DEVICE_ARCH}"
./reclaw.${DEVICE_ARCH}

pm_finish
RUN
chmod +x "$STAGE/ReClaw.sh"

cat > "$PKG/port.json" <<'JSON'
{
  "version": 3,
  "name": "reclaw.zip",
  "items": ["ReClaw.sh", "reclaw"],
  "items_opt": [],
  "attr": {
    "title": "ReClaw",
    "porter": ["Herbaciarz"],
    "desc": "Unofficial engine for Claw (1997), the pirate platformer by Monolith. Polish and English releases.",
    "inst": "Copy CLAW.REZ (and CLAW.FEC for the movies) from your own copy of Claw (1997) to ports/reclaw/data/en/ (English release) or data/pl/ (Polish release).",
    "genres": ["platformer"],
    "image": {"screenshot": "screenshot.png"},
    "rtr": false,
    "exp": false,
    "runtime": null,
    "reqs": [],
    "arch": ["aarch64"]
  }
}
JSON

cat > "$PKG/gameinfo.xml" <<'XML'
<?xml version="1.0"?>
<gameList>
  <game>
    <path>./ReClaw.sh</path>
    <name>ReClaw</name>
    <desc>Unofficial engine for Claw (1997), the pirate platformer by Monolith. Needs CLAW.REZ from your own copy of the game in ports/reclaw/data/en/ or data/pl/.</desc>
    <developer>Herbaciarz</developer>
    <genre>Platform</genre>
    <image>./reclaw/screenshot.png</image>
  </game>
</gameList>
XML

cp "$HERE/README-portmaster.md" "$PKG/README.md"
rm -f "$OUT/reclaw-portmaster.zip"
(cd "$STAGE" && zip -qr "$OUT/reclaw-portmaster.zip" ReClaw.sh reclaw)
rm -rf "$STAGE"
echo "$OUT/reclaw-portmaster.zip"
