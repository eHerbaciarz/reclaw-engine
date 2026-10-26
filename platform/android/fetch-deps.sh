#!/bin/bash
# Android: SDL2 sources (platform/deps) and SDL's Java glue for the app.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
"$HERE/../deps/fetch.sh"
mkdir -p "$HERE/app/src/main/java/org/libsdl"
rm -rf "$HERE/app/src/main/java/org/libsdl/app"
cp -r "$HERE/../deps/src/SDL/android-project/app/src/main/java/org/libsdl/app" "$HERE/app/src/main/java/org/libsdl/"
echo "Android dependencies ready"
