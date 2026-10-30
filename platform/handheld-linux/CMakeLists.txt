# Linux handheld builds: PortMaster (aarch64 handhelds: Anbernic, Powkiddy,
# Miyoo, Retroid...) and the Steam Deck (x86_64), cross-compiled with zig cc
# against an old glibc. The device's own SDL2 is used at run time; SDL2_image,
# SDL2_mixer and SDL2_ttf are linked in (platform/handheld-linux/build-deps.sh).
#
#   export ZIG_TARGET=aarch64-linux-gnu.2.27 RECLAW_DEPS=/opt/reclaw-deps/aarch64
#   platform/handheld-linux/build-deps.sh $ZIG_TARGET $RECLAW_DEPS
#   cmake -S platform/handheld-linux -B build-portmaster -DRECLAW_TARGET=portmaster \
#         -DCMAKE_TOOLCHAIN_FILE=platform/handheld-linux/zig.toolchain.cmake -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-portmaster -j
#   platform/handheld-linux/package-portmaster.sh build-portmaster <out dir>
#
# Steam Deck: ZIG_TARGET=x86_64-linux-gnu.2.31, -DRECLAW_TARGET=steamdeck,
# platform/handheld-linux/package-steamdeck.sh.

cmake_minimum_required(VERSION 3.16)
project(ReClawHandheldLinux C CXX)

set(RECLAW_TARGET "portmaster" CACHE STRING "portmaster or steamdeck")

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/OpenClawGame.cmake")

openclaw_add_game(openclaw)
openclaw_link_sdl(openclaw NO_GFX)
target_link_libraries(openclaw PRIVATE pthread m dl)
if (RECLAW_TARGET STREQUAL "steamdeck")
    target_compile_definitions(openclaw PRIVATE OPENCLAW_STEAMDECK=1)
else()
    target_compile_definitions(openclaw PRIVATE OPENCLAW_PORTMASTER=1)
endif()
# Startup steps and performance in save/reclaw.log (first builds on real hardware)
target_compile_definitions(openclaw PRIVATE OPENCLAW_STARTUP_TRACE=1)
# Look for the device's libSDL2-2.0.so.0 in the system first, then in libs/
set_target_properties(openclaw PROPERTIES OUTPUT_NAME "reclaw" BUILD_RPATH "" INSTALL_RPATH "")
target_link_options(openclaw PRIVATE "-Wl,-rpath,$ORIGIN/libs")
