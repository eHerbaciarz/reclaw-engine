#!/bin/bash
# Fetches the sources of SDL2 and its add-on libraries that the Android, 3DS
# and Wii U builds compile together with the game (platform/cmake/BundledSDL.cmake).
#   platform/deps/fetch.sh          upstream SDL (Android, 3DS)
#   platform/deps/fetch.sh wiiu     + devkitPro's Wii U SDL (GX2 renderer,
#                                     TV / GamePad windows)
set -e
SRC="$(cd "$(dirname "$0")" && pwd)/src"
mkdir -p "$SRC"
clone() { # url tag dir
    [ -d "$SRC/$3" ] || git clone -q --depth 1 -b "$2" "$1" "$SRC/$3"
}
clone https://github.com/libsdl-org/SDL       release-2.32.10 SDL
clone https://github.com/libsdl-org/SDL_image release-2.8.12  SDL_image
clone https://github.com/libsdl-org/SDL_mixer release-2.8.2   SDL_mixer
clone https://github.com/libsdl-org/SDL_ttf   release-2.24.0  SDL_ttf
# SDL_ttf's own copy of FreeType (the only vendored library needed: images use
# stb_image, sounds are WAV)
if [ ! -f "$SRC/SDL_ttf/external/freetype/CMakeLists.txt" ]; then
    git -C "$SRC/SDL_ttf" submodule update --init --depth 1 external/freetype
fi
if [ "$1" = "wiiu" ]; then
    clone https://github.com/devkitPro/SDL wiiu-sdl2-2.28 SDL-wiiu
fi
echo "sources ready in $SRC"
