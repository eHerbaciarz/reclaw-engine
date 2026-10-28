# PlayStation Vita build (VitaSDK) -> reclaw.vpk
#
#   cmake -S platform/vita -B build-vita -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
#   cmake --build build-vita -j
#
# Needs VitaSDK (with zlib) and platform/deps/fetch.sh: SDL2 and its add-ons
# are compiled from source with the game, like on 3DS / Wii U / Android.
# CI: .github/workflows/ports.yml
#
# Install: reclaw.vpk with VitaShell; CLAW.REZ from your copy of the game goes to
# ux0:data/reclaw/data/CLAW.REZ (config.xml, saves and reclaw.log go to
# ux0:data/reclaw/save/).

cmake_minimum_required(VERSION 3.16)

if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if (DEFINED ENV{VITASDK})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
    else()
        message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
    endif()
endif()

project(ClawVita C CXX)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "ReClaw")
set(VITA_TITLEID  "RCLW00001")
set(VITA_VERSION  "01.00")
# Memory for the game (newlib heap) is set in Engine/Platform/Platform.cpp.
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12")   # extended RAM mode

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/OpenClawGame.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/BundledSDL.cmake")

openclaw_add_game(openclaw)
openclaw_bundled_sdl(openclaw)
find_package(ZLIB REQUIRED)
target_link_libraries(openclaw PRIVATE ZLIB::ZLIB)
target_compile_options(openclaw PRIVATE -march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard)
target_link_libraries(openclaw PRIVATE
    SceNet_stub SceNetCtl_stub SceSysmodule_stub SceAppUtil_stub SceAppMgr_stub
    SceCommonDialog_stub ScePower_stub pthread m)

vita_create_self(eboot.bin openclaw UNSAFE)

set(BUNDLE "${CMAKE_BINARY_DIR}/bundle")
openclaw_stage_bundle(openclaw_bundle "${BUNDLE}" "${CMAKE_CURRENT_LIST_DIR}/config.xml")

vita_create_vpk(reclaw.vpk ${VITA_TITLEID} eboot.bin
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
    FILE ${CMAKE_CURRENT_LIST_DIR}/sce_sys/icon0.png sce_sys/icon0.png
    FILE ${CMAKE_CURRENT_LIST_DIR}/sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
    FILE ${CMAKE_CURRENT_LIST_DIR}/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
    FILE ${CMAKE_CURRENT_LIST_DIR}/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
    FILE ${BUNDLE}/config.xml config.xml
    FILE ${BUNDLE}/ASSETS.ZIP ASSETS.ZIP
    FILE ${BUNDLE}/clacon.ttf clacon.ttf
    FILE ${BUNDLE}/console02.tga console02.tga
    FILE ${BUNDLE}/splash.ttf splash.ttf
    FILE ${BUNDLE}/subtitles.ttf subtitles.ttf
    FILE ${BUNDLE}/music/soundfont.sf3 music/soundfont.sf3)
