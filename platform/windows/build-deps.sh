#!/bin/bash
# Cross-compiles the libraries the Windows build needs (SDL2, SDL2_image,
# SDL2_mixer, SDL2_ttf, SDL2_gfx, zlib, libpng, freetype) with MinGW-w64.
# Usage: platform/windows/build-deps.sh <source dir> <prefix>
#   <source dir> holds the unpacked sources (SDL2-2.28.5, SDL2_image-2.0.4, ...)
set -e
SRC="$1"; PREFIX="$2"
cd "$(dirname "$0")"; HERE="$(pwd)"; cd - >/dev/null
HOST=x86_64-w64-mingw32
export CC=$HOST-gcc-posix CXX=$HOST-g++-posix AR=$HOST-ar RANLIB=$HOST-ranlib
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig" PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"
export CPPFLAGS="-I$PREFIX/include" LDFLAGS="-L$PREFIX/lib"
export PATH="$PREFIX/bin:$PATH"
J=-j2
mkdir -p "$PREFIX" /tmp/wb

build_autotools() { # name dir extra-args...
  local name=$1 dir=$2; shift 2
  [ -f "$PREFIX/.done-$name" ] && return
  rm -rf /tmp/wb/$name; cp -a "$SRC/$dir" /tmp/wb/$name; cd /tmp/wb/$name
  { [ -f Makefile ] && make distclean >/dev/null 2>&1; } || true
  find . -name "*.o" -delete
  rm -f include/SDL_config.h
  mkdir -p _wb && cd _wb
  ../configure --host=$HOST --prefix="$PREFIX" --disable-static --enable-shared "$@"
  make $J && make install && touch "$PREFIX/.done-$name"
}

# zlib (own makefile for MinGW)
if [ ! -f "$PREFIX/.done-zlib" ]; then
  rm -rf /tmp/wb/zlib; cp -a "$SRC/zlib-1.3.1" /tmp/wb/zlib; cd /tmp/wb/zlib
  find . -name "*.o" -o -name "*.a" -o -name "*.so*" | xargs rm -f
  make -f win32/Makefile.gcc PREFIX=$HOST- SHARED_MODE=1 $J
  make -f win32/Makefile.gcc install SHARED_MODE=1 BINARY_PATH="$PREFIX/bin" INCLUDE_PATH="$PREFIX/include" LIBRARY_PATH="$PREFIX/lib"
  mkdir -p "$PREFIX/lib/pkgconfig"
  sed -e "s|@prefix@|$PREFIX|; s|@exec_prefix@|$PREFIX|; s|@libdir@|$PREFIX/lib|; s|@sharedlibdir@|$PREFIX/lib|; s|@includedir@|$PREFIX/include|; s|@VERSION@|1.3.1|" zlib.pc.in > "$PREFIX/lib/pkgconfig/zlib.pc"
  touch "$PREFIX/.done-zlib"
fi
build_autotools libpng libpng-1.6.48
# freetype: the source tree is a git checkout (no generated configure) -> CMake
if [ ! -f "$PREFIX/.done-freetype" ]; then
  rm -rf /tmp/wb/freetype-build
  cmake -S "$SRC/freetype-2.13.3" -B /tmp/wb/freetype-build \
    -DCMAKE_TOOLCHAIN_FILE="$HERE/mingw-toolchain.cmake" -DMINGW_PREFIX="$PREFIX" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
    -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON -DFT_DISABLE_BZIP2=ON -DFT_REQUIRE_PNG=ON -DFT_REQUIRE_ZLIB=ON \
    -DZLIB_ROOT="$PREFIX" -DPNG_ROOT="$PREFIX"
  cmake --build /tmp/wb/freetype-build $J && cmake --install /tmp/wb/freetype-build && touch "$PREFIX/.done-freetype"
fi
build_autotools sdl2 SDL2-2.28.5
build_autotools sdl2_image SDL2_image-2.0.4 --disable-jpg --disable-tif --disable-webp --enable-png --disable-png-shared
build_autotools sdl2_mixer SDL2_mixer-2.0.4 --disable-music-mod --disable-music-midi --disable-music-ogg --disable-music-flac --disable-music-mp3 --disable-music-opus
build_autotools sdl2_ttf SDL2_ttf-2.22.0 --disable-harfbuzz --disable-freetype-builtin
# SDL2_gfx: four C files, built straight into a DLL
if [ ! -f "$PREFIX/.done-sdl2_gfx" ]; then
  rm -rf /tmp/wb/gfx; cp -a "$SRC/SDL2_gfx" /tmp/wb/gfx; cd /tmp/wb/gfx; rm -f *.o
  $CC -O2 -shared -o "$PREFIX/bin/SDL2_gfx.dll" -Wl,--out-implib,"$PREFIX/lib/libSDL2_gfx.dll.a" \
      -I"$PREFIX/include/SDL2" SDL2_framerate.c SDL2_gfxPrimitives.c SDL2_imageFilter.c SDL2_rotozoom.c \
      -L"$PREFIX/lib" -lSDL2
  cp SDL2_framerate.h SDL2_gfxPrimitives.h SDL2_imageFilter.h SDL2_rotozoom.h SDL2_gfxPrimitives_font.h "$PREFIX/include/SDL2/"
  cat > "$PREFIX/lib/pkgconfig/SDL2_gfx.pc" <<PC
prefix=$PREFIX
libdir=\${prefix}/lib
includedir=\${prefix}/include
Name: SDL2_gfx
Description: SDL2 graphics primitives
Version: 1.0.4
Requires: sdl2
Libs: -L\${libdir} -lSDL2_gfx
Cflags: -I\${includedir}/SDL2
PC
  touch "$PREFIX/.done-sdl2_gfx"
fi
echo "deps done in $PREFIX"
