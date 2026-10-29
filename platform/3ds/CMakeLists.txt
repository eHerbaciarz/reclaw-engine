# ReClaw - Nintendo 3DS build (devkitARM + libctru) -> reclaw.3dsx
#
#   cmake -S platform/3ds -B build-3ds -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake
#   cmake --build build-3ds -j
#
# Needs: (dkp-)pacman -S 3ds-dev 3ds-zlib, and platform/deps/fetch.sh
# (SDL2 and its add-ons are compiled from source with the game).
# CI: .github/workflows/3ds.yml (devkitpro/devkitarm image)
#
# The game runs on the top screen (400x240); the bottom screen shows the main
# menu's map with the player's status (Engine/UserInterface/SecondScreen.cpp).
# Install: reclaw.3dsx to sdmc:/3ds/reclaw/ (or reclaw.cia with FBI); the first
# start creates sdmc:/3ds/reclaw/ with data/pl/, data/en/, ... - CLAW.REZ (+ CLAW.FEC)
# from your copy of the game goes to data/pl/ or data/en/. Use a New 3DS / 2DS XL:
# the old models have too little memory and CPU for the game.

cmake_minimum_required(VERSION 3.16)
project(ClawN3DS C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/OpenClawGame.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/BundledSDL.cmake")

openclaw_add_game(openclaw)
openclaw_bundled_sdl(openclaw)          # SDL's main() (SDL2main) sets up the screens
find_package(ZLIB REQUIRED)
target_link_libraries(openclaw PRIVATE ZLIB::ZLIB ctru m)
# Startup trace and fps every 5 s in reclaw.log (first 3DS builds).
target_compile_definitions(openclaw PRIVATE OPENCLAW_STARTUP_TRACE)

set(BUNDLE "${CMAKE_BINARY_DIR}/romfs")
openclaw_stage_bundle(openclaw_bundle "${BUNDLE}" "${CMAKE_CURRENT_LIST_DIR}/config.xml")
dkp_add_asset_target(claw_romfs "${BUNDLE}")
set_property(TARGET claw_romfs APPEND PROPERTY DKP_ASSET_FILES ${OPENCLAW_BUNDLE_FILES})

ctr_generate_smdh(reclaw.smdh
    NAME        "ReClaw"
    DESCRIPTION "Engine for Claw (1997) - needs CLAW.REZ"
    AUTHOR      "OpenClaw, claw-web, Herbaciarz"
    ICON        "${CMAKE_CURRENT_LIST_DIR}/icon.png")

ctr_create_3dsx(openclaw
    OUTPUT "${CMAKE_BINARY_DIR}/reclaw.3dsx"
    SMDH   "${CMAKE_BINARY_DIR}/reclaw.smdh"
    ROMFS  claw_romfs)
