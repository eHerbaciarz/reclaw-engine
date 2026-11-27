#!/bin/bash
# macOS package: ReClaw.app in reclaw-macos-arm64.zip
#   ReClaw.app/Contents/MacOS/openclaw      the engine
#   ReClaw.app/Contents/Frameworks/         SDL2 libraries (from Homebrew, relinked)
#   ReClaw.app/Contents/Resources/          bundled files (ASSETS.ZIP, fonts, ...)
# No game data: on its first start the game creates ~/Documents/ReClaw/ with
# data/pl/, data/en/ ... for CLAW.REZ from the player's own copy.
# Usage: platform/macos/package.sh <build dir with openclaw> <out dir>
set -e
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$1"; OUT="$2"
[ -x "$BUILD/openclaw" ] || { echo "usage: $0 <build dir> <out dir>"; exit 1; }
ARCH="$(uname -m)"
APP="$OUT/ReClaw.app"
rm -rf "$APP"; mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$APP/Contents/Frameworks"

"$REPO/scripts/stage_bundle.sh" "$APP/Contents/Resources"
cp "$BUILD/openclaw" "$APP/Contents/MacOS/openclaw"
strip -x "$APP/Contents/MacOS/openclaw" || true

# Homebrew's SDL2 libraries into the app, with paths relative to it
dylibbundler -of -cd -b -x "$APP/Contents/MacOS/openclaw" \
    -d "$APP/Contents/Frameworks/" -p "@executable_path/../Frameworks/"
# Homebrew's SDL2 is sdl2-compat: it loads SDL3 with dlopen() at run time,
# which dylibbundler cannot see. It looks next to itself (@loader_path) first.
SDL3=""
if command -v brew >/dev/null && [ -d "$(brew --prefix sdl3 2>/dev/null)/lib" ]; then
    SDL3="$(brew --prefix sdl3)/lib/libSDL3.0.dylib"
fi
if [ -n "$SDL3" ] && [ -f "$SDL3" ]; then
    cp -L "$SDL3" "$APP/Contents/Frameworks/libSDL3.0.dylib"
    chmod u+w "$APP/Contents/Frameworks/libSDL3.0.dylib"
    install_name_tool -id "@loader_path/libSDL3.0.dylib" "$APP/Contents/Frameworks/libSDL3.0.dylib"
    # SDL3's own non-system dependencies (normally none)
    otool -L "$APP/Contents/Frameworks/libSDL3.0.dylib" | tail -n +2 | awk '{print $1}' | grep -E "^/opt/homebrew|^/usr/local" || true
fi

# Icon (macOS makes the .icns from a set of PNG sizes)
ICONSET="$OUT/ReClaw.iconset"
rm -rf "$ICONSET"; mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
    sips -z $s $s "$REPO/platform/icon/reclaw-icon.png" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
    d=$((s * 2))
    sips -z $d $d "$REPO/platform/icon/reclaw-icon.png" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/ReClaw.icns"
rm -rf "$ICONSET"

VERSION="$(cd "$REPO" && git describe --tags --always 2>/dev/null || echo 0.1)"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>ReClaw</string>
    <key>CFBundleDisplayName</key><string>ReClaw</string>
    <key>CFBundleIdentifier</key><string>io.github.eherbaciarz.reclaw</string>
    <key>CFBundleVersion</key><string>${VERSION}</string>
    <key>CFBundleShortVersionString</key><string>0.1</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleExecutable</key><string>openclaw</string>
    <key>CFBundleIconFile</key><string>ReClaw</string>
    <key>LSMinimumSystemVersion</key><string>14.0</string>
    <key>LSApplicationCategoryType</key><string>public.app-category.arcade-games</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>GCSupportsControllerUserInteraction</key><true/>
</dict>
</plist>
PLIST

# No Apple developer certificate: an ad-hoc signature (Apple Silicon does not
# run unsigned code at all). The first start needs right click > Open.
# Nothing may still point into Homebrew (the player's Mac has no Homebrew)
LEFT=$(for f in "$APP/Contents/MacOS/openclaw" "$APP/Contents/Frameworks/"*.dylib; do otool -L "$f" | tail -n +2 | awk '{print $1}'; done | grep -E "^/opt/homebrew|^/usr/local" | sort -u || true)
if [ -n "$LEFT" ]; then echo "still linked to Homebrew:"; echo "$LEFT"; exit 1; fi
ls "$APP/Contents/Frameworks"
codesign --force --deep --sign - "$APP"

cp "$REPO/platform/macos/README.txt" "$OUT/README.txt"
(cd "$OUT" && ditto -c -k --keepParent ReClaw.app "reclaw-macos-$ARCH.zip" && zip -q "reclaw-macos-$ARCH.zip" README.txt)
echo "$OUT/reclaw-macos-$ARCH.zip"
