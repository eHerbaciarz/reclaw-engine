#!/bin/bash
# Builds SDL2_image and SDL2_ttf for the PS3 into $PS3DEV/portlibs/ppu
# (ps3libraries provides SDL2 and SDL2_mixer but not these two).
# Versions that still build against the PSL1GHT SDL port (2.0.13).
set -e
: "${PS3DEV:?set PS3DEV}"
PSL1GHT="${PSL1GHT:-$PS3DEV}"
PORTLIBS="$PS3DEV/portlibs/ppu"
WORK="${1:-/tmp/ps3-sdl-addons}"
export PATH="$PS3DEV/bin:$PS3DEV/ppu/bin:$PATH"
export PKG_CONFIG_LIBDIR="$PORTLIBS/lib/pkgconfig"
export PKG_CONFIG_PATH=""
export CFLAGS="-O2 -mcpu=cell -I$PSL1GHT/ppu/include -I$PORTLIBS/include"
export CPPFLAGS="$CFLAGS"
export LDFLAGS="-L$PSL1GHT/ppu/lib -L$PORTLIBS/lib -lrt -llv2"
mkdir -p "$WORK" && cd "$WORK"

fetch() { # repo tag dir
    [ -d "$3" ] || git clone -q --depth 1 -b "$2" "https://github.com/libsdl-org/$1" "$3"
}

fetch SDL_image release-2.0.5 SDL2_image
(cd SDL2_image && ./configure --host=powerpc64-ps3-elf --prefix="$PORTLIBS" \
    --disable-shared --enable-static --disable-sdltest \
    --with-sdl-prefix="$PORTLIBS" \
    --enable-png --disable-png-shared --disable-jpg --disable-tif --disable-webp \
    --disable-jpg-shared --disable-tif-shared --disable-webp-shared \
    && make -j"$(nproc)" && make install)

fetch SDL_ttf release-2.0.15 SDL2_ttf
(cd SDL2_ttf && ./configure --host=powerpc64-ps3-elf --prefix="$PORTLIBS" \
    --disable-shared --enable-static --disable-sdltest \
    --with-sdl-prefix="$PORTLIBS" --with-freetype-prefix="$PORTLIBS" \
    && make -j"$(nproc)" && make install)

echo "SDL2_image and SDL2_ttf installed to $PORTLIBS"
