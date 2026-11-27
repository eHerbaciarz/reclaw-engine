# ReClaw - Sega Dreamcast build (KallistiOS + GPF's SDL2 Dreamcast port)
#   -> reclaw.elf, 1ST_READ.BIN
#
#   source /opt/toolchains/dc/kos/environ.sh
#   cmake -S platform/dreamcast -B build-dc \
#         -DCMAKE_TOOLCHAIN_FILE=$KOS_BASE/utils/cmake/kallistios.toolchain.cmake \
#         -DRECLAW_DC_SDL=<GPF/SDL2 dreamcastSDL2 checkout>
#   cmake --build build-dc -j
#
# EXPERIMENTAL: the Dreamcast has 16 MB of RAM; the engine was written for far
# more and is not expected to get far. The disc holds the game files (/cd/),
# saves and the log go to the RAM disk (/ram/save/). CI: .github/workflows/ports.yml

cmake_minimum_required(VERSION 3.16)
project(ReClawDreamcast C CXX)

set(RECLAW_DC_SDL "" CACHE PATH "GPF/SDL2 (branch dreamcastSDL2) source tree")
if (NOT EXISTS "${RECLAW_DC_SDL}/CMakeLists.txt")
    message(FATAL_ERROR "RECLAW_DC_SDL must point to a checkout of https://github.com/GPF/SDL2 (dreamcastSDL2)")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/OpenClawGame.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/BundledSDL.cmake")

# Upstream SDL_image / SDL_mixer / SDL_ttf over the Dreamcast SDL2
set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
# The native PVR renderer needs a newer KallistiOS than the CI image has
# (pvr_scene_begin_rtt, PVR_LIST_NONE): SDL's software renderer for now.
set(SDL_VIDEO_RENDER_DREAMCAST_PVR OFF CACHE BOOL "" FORCE)
openclaw_add_game(openclaw)
openclaw_bundled_sdl(openclaw SDL_DIR "${RECLAW_DC_SDL}")
target_compile_definitions(openclaw PRIVATE RECLAW_DREAMCAST=1 OPENCLAW_STARTUP_TRACE=1 RECLAW_TEXTURE_16BIT=1)
target_compile_options(openclaw PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${CMAKE_CURRENT_LIST_DIR}/dc_cxx_compat.h>")

find_package(ZLIB QUIET)
if (ZLIB_FOUND)
    target_link_libraries(openclaw PRIVATE ZLIB::ZLIB)
else()
    target_link_libraries(openclaw PRIVATE z)
endif()
# SDL's Dreamcast video driver always contains its GLdc glue (kos-ports libGL)
target_sources(openclaw PRIVATE "${CMAKE_CURRENT_LIST_DIR}/dc_sdl_compat.c")
target_link_libraries(openclaw PRIVATE GL m)
set_target_properties(openclaw PROPERTIES OUTPUT_NAME "reclaw" SUFFIX ".elf")

# Files that go on the disc next to 1ST_READ.BIN (no game data)
openclaw_stage_bundle(openclaw_bundle "${CMAKE_BINARY_DIR}/disc" "${CMAKE_CURRENT_LIST_DIR}/config.xml")
