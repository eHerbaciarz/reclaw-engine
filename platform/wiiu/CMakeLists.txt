# ReClaw - Wii U build (devkitPPC + wut) -> reclaw.wuhb
#
#   cmake -S platform/wiiu -B build-wiiu -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake
#   cmake --build build-wiiu -j
#
# Needs: (dkp-)pacman -S wiiu-dev ppc-zlib, and platform/deps/fetch.sh wiiu
# (devkitPro's Wii U SDL2 and the add-ons are compiled from source with the game).
# CI: .github/workflows/wiiu.yml (devkitpro/devkitppc image)
#
# The game is shown on the TV, the GamePad shows the main menu's map with the
# player's status (Engine/UserInterface/SecondScreen.cpp).
# Install: reclaw.wuhb to sd:/wiiu/apps/ (Aroma); CLAW.REZ from your copy of the
# game to sd:/wiiu/apps/reclaw/data/CLAW.REZ.

cmake_minimum_required(VERSION 3.16)
project(ClawWiiU C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/OpenClawGame.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/BundledSDL.cmake")

openclaw_add_game(openclaw)
openclaw_bundled_sdl(openclaw SDL_DIR "${OPENCLAW_DEPS}/SDL-wiiu")
find_package(ZLIB REQUIRED)
target_link_libraries(openclaw PRIVATE ZLIB::ZLIB m)

wut_create_rpx(openclaw)

# The bundled files become the .wuhb's content folder (fs:/vol/content/).
set(BUNDLE "${CMAKE_BINARY_DIR}/content")
openclaw_stage_bundle(openclaw_bundle "${BUNDLE}" "${CMAKE_CURRENT_LIST_DIR}/config.xml")
dkp_add_asset_target(claw_content "${BUNDLE}")
set_property(TARGET claw_content APPEND PROPERTY DKP_ASSET_FILES ${OPENCLAW_BUNDLE_FILES})

wut_create_wuhb(reclaw
    TARGET    openclaw
    OUTPUT    "${CMAKE_BINARY_DIR}/reclaw.wuhb"
    CONTENT   claw_content
    NAME      "ReClaw"
    SHORTNAME "ReClaw"
    AUTHOR    "OpenClaw, claw-web, MK & Claude"
    ICON      "${CMAKE_CURRENT_LIST_DIR}/icon.png"
    TVSPLASH  "${CMAKE_CURRENT_LIST_DIR}/tv-splash.png"
    DRCSPLASH "${CMAKE_CURRENT_LIST_DIR}/drc-splash.png")
