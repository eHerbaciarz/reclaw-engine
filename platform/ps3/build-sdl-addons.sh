#!/bin/bash
# Builds SDL2_image and SDL2_ttf for the PS3 into $PS3DEV/portlibs/ppu
# (ps3libraries provides SDL2 and SDL2_mixer but not these two).
# Versions that still build against the PSL1GHT SDL port (2.0.13).
# Compiled directly (no autotools: their old generated scripts clash with
# current libtool), with hand-written pkg-config files.
set -e
: "${PS3DEV:?set PS3DEV}"
PSL1GHT="${PSL1GHT:-$PS3DEV}"
PORTLIBS="$PS3DEV/portlibs/ppu"
WORK="${1:-/tmp/ps3-sdl-addons}"
export PATH="$PS3DEV/bin:$PS3DEV/ppu/bin:$PATH"
CC=powerpc64-ps3-elf-gcc
AR=powerpc64-ps3-elf-ar
CFLAGS="-O2 -mcpu=cell -I$PSL1GHT/ppu/include -I$PORTLIBS/include -I$PORTLIBS/include/SDL2"
mkdir -p "$WORK" && cd "$WORK"

fetch() { # repo tag dir
    [ -d "$3" ] || git clone -q --depth 1 -b "$2" "https://github.com/libsdl-org/$1" "$3"
}

# libpng 1.6 (ps3libraries has 1.4, too old for SDL2_image's PNG loader -
# with it IMG_png.c did not build and the PNG functions were missing)
if [ ! -f "$PORTLIBS/include/libpng16/png.h" ]; then
    [ -d libpng ] || git clone -q --depth 1 -b v1.6.43 https://github.com/pnggroup/libpng libpng
    (cd libpng && rm -f *.o &&
        cp scripts/pnglibconf.h.prebuilt pnglibconf.h &&
        for f in png.c pngerror.c pngget.c pngmem.c pngpread.c pngread.c pngrio.c pngrtran.c \
                 pngrutil.c pngset.c pngtrans.c pngwio.c pngwrite.c pngwtran.c pngwutil.c; do
            $CC $CFLAGS -DPNG_ARM_NEON_OPT=0 -DPNG_POWERPC_VSX_OPT=0 -c "$f" -o "${f%.c}.o" || exit 1
        done &&
        $AR rcs libpng16.a *.o &&
        mkdir -p "$PORTLIBS/include/libpng16" &&
        install -m644 png.h pngconf.h pnglibconf.h "$PORTLIBS/include/libpng16/" &&
        install -m644 libpng16.a "$PORTLIBS/lib/")
fi

# SDL2_image: BMP, PCX, PNG, TGA (the game uses PCX and PNG)
fetch SDL_image release-2.0.5 SDL2_image
(cd SDL2_image && rm -f *.o &&
    for f in IMG.c IMG_bmp.c IMG_gif.c IMG_jpg.c IMG_lbm.c IMG_pcx.c IMG_png.c IMG_pnm.c \
             IMG_svg.c IMG_tga.c IMG_tif.c IMG_webp.c IMG_xcf.c IMG_xpm.c IMG_xv.c IMG_xxx.c; do
        $CC $CFLAGS -I"$PORTLIBS/include/libpng16" -DLOAD_BMP -DLOAD_PCX -DLOAD_PNG -DLOAD_TGA \
            -c "$f" -o "${f%.c}.o" || exit 1
    done &&
    $AR rcs libSDL2_image.a *.o &&
    install -m644 libSDL2_image.a "$PORTLIBS/lib/" &&
    install -m644 SDL_image.h "$PORTLIBS/include/SDL2/")
[ -f "$PORTLIBS/lib/libpng16.a" ] || ln -sf libpng.a "$PORTLIBS/lib/libpng16.a"
cat > "$PORTLIBS/lib/pkgconfig/SDL2_image.pc" <<EOF
prefix=$PORTLIBS
libdir=\${prefix}/lib
includedir=\${prefix}/include
Name: SDL2_image
Description: image loading library for Simple DirectMedia Layer
Version: 2.0.5
Requires: sdl2 >= 2.0.8
Libs: -L\${libdir} -lSDL2_image -lpng16 -lz
Cflags: -I\${includedir}/SDL2
EOF

# SDL2_ttf (FreeType from portlibs)
fetch SDL_ttf release-2.0.15 SDL2_ttf
(cd SDL2_ttf &&
    $CC $CFLAGS -I"$PORTLIBS/include/freetype2" -c SDL_ttf.c -o SDL_ttf.o &&
    $AR rcs libSDL2_ttf.a SDL_ttf.o &&
    install -m644 libSDL2_ttf.a "$PORTLIBS/lib/" &&
    install -m644 SDL_ttf.h "$PORTLIBS/include/SDL2/")
cat > "$PORTLIBS/lib/pkgconfig/SDL2_ttf.pc" <<EOF
prefix=$PORTLIBS
libdir=\${prefix}/lib
includedir=\${prefix}/include
Name: SDL2_ttf
Description: ttf library for Simple DirectMedia Layer with FreeType 2 support
Version: 2.0.15
Requires: sdl2 >= 2.0.8 freetype2
Libs: -L\${libdir} -lSDL2_ttf
Cflags: -I\${includedir}/SDL2
EOF

echo "SDL2_image and SDL2_ttf installed to $PORTLIBS"
