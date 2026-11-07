#!/bin/bash
# A minimal static FFmpeg for the remastered movies (movies/movie_XX.mp4):
# only the MP4 demuxer and the H.264 and AAC decoders (libavformat,
# libavcodec, libavutil; LGPL, no GPL parts).
#
#   scripts/build_ffmpeg_min.sh <platform> <prefix> [FFmpeg source]
#   platform: linux | steamdeck | portmaster | windows | switch | ps4 | ps3
# steamdeck / portmaster use platform/handheld-linux's zig cc (ZIG_TARGET is
# set here), switch needs DEVKITPRO, ps4 the OpenOrbis toolchain (OPENORBIS).
set -e
PLATFORM="$1"; PREFIX="$2"; SRC="${3:-/tmp/ffmpeg-src}"
[ -n "$PLATFORM" ] && [ -n "$PREFIX" ] || { echo "usage: $0 <platform> <prefix> [source]"; exit 1; }
REPO="$(cd "$(dirname "$0")/.." && pwd)"
[ -d "$SRC" ] || git clone -q --depth 1 -b n6.1.2 https://github.com/FFmpeg/FFmpeg "$SRC"
BUILD="$SRC/build-$PLATFORM"
rm -rf "$BUILD"; mkdir -p "$BUILD"; cd "$BUILD"

COMMON=(--prefix="$PREFIX" --enable-static --disable-shared --enable-pic
    --disable-programs --disable-doc --disable-debug --disable-avdevice --disable-swscale
    --disable-swresample --disable-postproc --disable-avfilter --disable-network
    --disable-everything --enable-protocol=file --enable-demuxer=mov
    --enable-decoder=h264 --enable-decoder=aac --enable-parser=h264 --enable-parser=aac
    --disable-iconv --disable-zlib --disable-bzlib --disable-lzma --disable-xlib --disable-sdl2
    --disable-vaapi --disable-vdpau --disable-vulkan --disable-cuda-llvm --disable-ffnvcodec
    --disable-autodetect)
HL="$REPO/platform/handheld-linux"

case "$PLATFORM" in
  linux)
    ../configure "${COMMON[@]}" --enable-pthreads ;;
  steamdeck|portmaster)
    if [ "$PLATFORM" = steamdeck ]; then export ZIG_TARGET=x86_64-linux-gnu.2.31; ARCH=x86_64; EXTRA=(--disable-x86asm)
    else export ZIG_TARGET=aarch64-linux-gnu.2.27; ARCH=aarch64; EXTRA=(); fi
    ../configure "${COMMON[@]}" "${EXTRA[@]}" --enable-cross-compile --target-os=linux --arch=$ARCH \
        --cc="$HL/zig-cc" --cxx="$HL/zig-c++" --ar="$HL/zig-ar" --ranlib="$HL/zig-ranlib" \
        --nm=llvm-nm-18 --strip=llvm-strip-18 --enable-pthreads ;;
  windows)
    ../configure "${COMMON[@]}" --enable-cross-compile --target-os=mingw32 --arch=x86_64 \
        --cross-prefix=x86_64-w64-mingw32- --enable-w32threads ;;
  switch)
    : "${DEVKITPRO:?set DEVKITPRO}"
    export PATH="$DEVKITPRO/devkitA64/bin:$PATH"
    ../configure "${COMMON[@]}" --enable-cross-compile --target-os=none --arch=aarch64 \
        --cross-prefix=aarch64-none-elf- --enable-pthreads \
        --extra-cflags="-D__SWITCH__ -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -I$DEVKITPRO/libnx/include -I$DEVKITPRO/portlibs/switch/include" \
        --extra-ldflags="-specs=$DEVKITPRO/libnx/switch.specs -march=armv8-a+crc+crypto -mtp=soft -fPIE -L$DEVKITPRO/libnx/lib -L$DEVKITPRO/portlibs/switch/lib" \
        --extra-libs="-lnx" --disable-runtime-cpudetect ;;
  ps4)
    : "${OPENORBIS:?source ps4vars.sh}"
    ../configure "${COMMON[@]}" --enable-cross-compile --target-os=freebsd --arch=x86_64 --disable-x86asm \
        --cc="$OPENORBIS/bin/clang" --ar="$OPENORBIS/bin/llvm-ar" --ranlib="$OPENORBIS/bin/llvm-ranlib" \
        --nm="$OPENORBIS/bin/llvm-nm" --strip="$OPENORBIS/bin/llvm-strip" --enable-pthreads \
        --extra-cflags="--target=x86_64-pc-freebsd12-elf -D__PS4__ -D__OPENORBIS__ -isysroot $OPENORBIS -isystem $OPENORBIS/include -fPIC" \
        --extra-ldflags="--target=x86_64-pc-freebsd12-elf -fuse-ld=lld -nostdlib -L$OPENORBIS/lib" \
        --disable-linux-perf ;;
  ps3)
    # PSL1GHT (Cell PPU, 64-bit big-endian PowerPC with AltiVec); no pthreads:
    # one decoding thread
    : "${PS3DEV:?set PS3DEV}"
    export PSL1GHT="${PSL1GHT:-$PS3DEV}" PATH="$PS3DEV/ppu/bin:$PATH"
    ../configure "${COMMON[@]}" --enable-cross-compile --target-os=none --arch=ppc64 --cpu=cell \
        --cross-prefix=ppu- --disable-pthreads --enable-altivec --disable-runtime-cpudetect \
        --extra-cflags="-D__PSL1GHT__ -mcpu=cell -maltivec -I$PS3DEV/ppu/include -I$PS3DEV/portlibs/ppu/include" \
        --extra-ldflags="-L$PS3DEV/ppu/lib -L$PS3DEV/portlibs/ppu/lib" ;;
  *) echo "unknown platform $PLATFORM"; exit 1 ;;
esac
# zig's glibc and the OpenOrbis headers have no <sys/sysctl.h> although configure finds sysctl()
case "$PLATFORM" in steamdeck|portmaster|ps4) sed -i 's/^#define HAVE_SYSCTL 1/#define HAVE_SYSCTL 0/' config.h ;; esac
make -j"$(nproc)"
make install
echo "FFmpeg (h264 + aac, mp4) installed to $PREFIX"
