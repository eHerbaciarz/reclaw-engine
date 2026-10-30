#!/bin/bash
# Libraries for the Linux handheld builds (PortMaster, Steam Deck), built with
# zig cc against an old glibc:
#   SDL2      shared, only to link against: at run time the device's own
#             libSDL2-2.0.so.0 is used (it knows the screen and the buttons).
#             SDL 2.0.18 headers, so only functions every current firmware has.
#   SDL2_image 2.0.5 (PNG/PCX/BMP/TGA), SDL2_mixer 2.0.4 (WAV), SDL2_ttf 2.0.15,
#   FreeType, libpng, zlib: static, linked into the game.
#
#   platform/handheld-linux/build-deps.sh <zig target> <prefix> [work dir]
#   e.g. build-deps.sh aarch64-linux-gnu.2.27 /opt/reclaw-deps/aarch64
set -e
export ZIG_TARGET="$1"; PREFIX="$2"; WORK="${3:-/tmp/reclaw-handheld-deps}"
[ -n "$ZIG_TARGET" ] && [ -n "$PREFIX" ] || { echo "usage: $0 <zig target> <prefix> [work dir]"; exit 1; }
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
CC="$HERE/zig-cc"; AR="$HERE/zig-ar"
TC="$HERE/zig.toolchain.cmake"
export RECLAW_DEPS="$PREFIX"
mkdir -p "$WORK" "$PREFIX/lib/pkgconfig" "$PREFIX/include/SDL2"
cd "$WORK"

fetch() { # url tag dir
    [ -d "$3" ] || git clone -q --depth 1 -b "$2" "$1" "$3"
}
fetch https://github.com/libsdl-org/SDL release-2.0.18 SDL2
fetch https://github.com/madler/zlib v1.2.13 zlib
fetch https://github.com/pnggroup/libpng v1.6.43 libpng
fetch https://github.com/libsdl-org/SDL_image release-2.0.5 SDL2_image
fetch https://github.com/libsdl-org/SDL_mixer release-2.0.4 SDL2_mixer
fetch https://github.com/libsdl-org/SDL_ttf release-2.0.15 SDL2_ttf
FREETYPE="$REPO/platform/deps/src/SDL_ttf/external/freetype"
[ -f "$FREETYPE/CMakeLists.txt" ] || { echo "run platform/deps/fetch.sh first (FreeType)"; exit 1; }

cm() { # src build args...
    local s=$1 b=$2; shift 2
    rm -rf "$b"
    cmake -S "$s" -B "$b" -DCMAKE_TOOLCHAIN_FILE="$TC" -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON "$@"
    cmake --build "$b" -j"$(nproc)"
    cmake --install "$b"
}

# SDL2: a shared library with no backends (link stub, not shipped)
if [ ! -f "$PREFIX/lib/libSDL2-2.0.so.0" ]; then
    cm SDL2 b-sdl2 -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF \
        -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF \
        -DSDL_VULKAN=OFF -DSDL_ALSA=OFF -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF \
        -DSDL_SNDIO=OFF -DSDL_ESD=OFF -DSDL_ARTS=OFF -DSDL_NAS=OFF -DSDL_LIBSAMPLERATE=OFF \
        -DSDL_HIDAPI=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_RPI=OFF -DSDL_DIRECTFB=OFF -DSDL_OSS=OFF
fi
cat > "$PREFIX/lib/pkgconfig/sdl2.pc" <<EOF
prefix=$PREFIX
libdir=\${prefix}/lib
includedir=\${prefix}/include
Name: sdl2
Description: Simple DirectMedia Layer (the device's own library at run time)
Version: 2.0.18
Libs: -L\${libdir} -lSDL2
Libs.private:
Cflags: -I\${includedir}/SDL2 -D_REENTRANT
EOF

if [ ! -f "$PREFIX/lib/libz.a" ]; then
    (cd zlib && make distclean >/dev/null 2>&1 || true
     CC="$CC" AR="$AR" RANLIB="$HERE/zig-ranlib" CFLAGS="-O2 -fPIC" ./configure --static --prefix="$PREFIX" &&
     make -j"$(nproc)" libz.a && make install)
fi
rm -f "$PREFIX"/lib/libz.so*
cat > "$PREFIX/lib/pkgconfig/zlib.pc" <<EOF
prefix=$PREFIX
Name: zlib
Description: zlib compression library
Version: 1.2.13
Libs: -L\${prefix}/lib -l:libz.a
Cflags: -I\${prefix}/include
EOF

[ -f "$PREFIX/lib/libpng16.a" ] || cm libpng b-png -DPNG_SHARED=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF \
    -DPNG_EXECUTABLES=OFF -DPNG_HARDWARE_OPTIMIZATIONS=OFF \
    -DZLIB_LIBRARY="$PREFIX/lib/libz.a" -DZLIB_INCLUDE_DIR="$PREFIX/include"
[ -f "$PREFIX/lib/libpng16.a" ] || ln -sf libpng.a "$PREFIX/lib/libpng16.a"

[ -f "$PREFIX/lib/libfreetype.a" ] || cm "$FREETYPE" b-ft -DBUILD_SHARED_LIBS=OFF \
    -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON -DFT_DISABLE_BROTLI=ON \
    -DZLIB_LIBRARY="$PREFIX/lib/libz.a" -DZLIB_INCLUDE_DIR="$PREFIX/include"

CFLAGS="-O2 -fPIC -I$PREFIX/include -I$PREFIX/include/SDL2 -D_REENTRANT"

# SDL2_image: BMP, PCX, PNG, TGA
(cd SDL2_image && rm -f *.o &&
    for f in IMG.c IMG_bmp.c IMG_gif.c IMG_jpg.c IMG_lbm.c IMG_pcx.c IMG_png.c IMG_pnm.c \
             IMG_svg.c IMG_tga.c IMG_tif.c IMG_webp.c IMG_xcf.c IMG_xpm.c IMG_xv.c IMG_xxx.c; do
        "$CC" $CFLAGS -I"$PREFIX/include/libpng16" -DLOAD_BMP -DLOAD_PCX -DLOAD_PNG -DLOAD_TGA -c "$f" -o "${f%.c}.o"
    done &&
    rm -f libSDL2_image.a && "$AR" rcs libSDL2_image.a *.o &&
    install -m644 libSDL2_image.a "$PREFIX/lib/" && install -m644 SDL_image.h "$PREFIX/include/SDL2/")
cat > "$PREFIX/lib/pkgconfig/SDL2_image.pc" <<EOF
prefix=$PREFIX
Name: SDL2_image
Description: image loading library for Simple DirectMedia Layer
Version: 2.0.5
Requires: sdl2
Libs: -L\${prefix}/lib -l:libSDL2_image.a -l:libpng16.a -l:libz.a
Cflags: -I\${prefix}/include/SDL2
EOF

# SDL2_mixer: WAV, AIFF, VOC sounds and WAV music
(cd SDL2_mixer && rm -f *.o &&
    for f in mixer.c music.c effect_position.c effect_stereoreverse.c effects_internal.c \
             load_aiff.c load_voc.c music_wav.c; do
        "$CC" $CFLAGS -DMUSIC_WAV -c "$f" -o "${f%.c}.o"
    done &&
    rm -f libSDL2_mixer.a && "$AR" rcs libSDL2_mixer.a *.o &&
    install -m644 libSDL2_mixer.a "$PREFIX/lib/" && install -m644 SDL_mixer.h "$PREFIX/include/SDL2/")
cat > "$PREFIX/lib/pkgconfig/SDL2_mixer.pc" <<EOF
prefix=$PREFIX
Name: SDL2_mixer
Description: mixer library for Simple DirectMedia Layer
Version: 2.0.4
Requires: sdl2
Libs: -L\${prefix}/lib -l:libSDL2_mixer.a
Cflags: -I\${prefix}/include/SDL2
EOF

# SDL2_ttf
(cd SDL2_ttf && "$CC" $CFLAGS -I"$PREFIX/include/freetype2" -c SDL_ttf.c -o SDL_ttf.o &&
    rm -f libSDL2_ttf.a && "$AR" rcs libSDL2_ttf.a SDL_ttf.o &&
    install -m644 libSDL2_ttf.a "$PREFIX/lib/" && install -m644 SDL_ttf.h "$PREFIX/include/SDL2/")
cat > "$PREFIX/lib/pkgconfig/SDL2_ttf.pc" <<EOF
prefix=$PREFIX
Name: SDL2_ttf
Description: ttf library for Simple DirectMedia Layer
Version: 2.0.15
Requires: sdl2
Libs: -L\${prefix}/lib -l:libSDL2_ttf.a -l:libfreetype.a -l:libz.a
Cflags: -I\${prefix}/include/SDL2
EOF

echo "dependencies ready in $PREFIX"
