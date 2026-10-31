# ReClaw - PS4 build (OpenOrbis toolchain from PacBrew, SDL2 port by cpasjuste)
#   -> IV0001-RCLW00002_00-RCLW000020100000.pkg
#
#   (pacman) ps4-openorbis ps4-openorbis-portlibs     see platform/ps4/README.txt
#   source /opt/pacbrew/ps4/openorbis/ps4vars.sh
#   openorbis-cmake -S platform/ps4 -B build-ps4 && cmake --build build-ps4 -j
#
# Install the .pkg with GoldHEN's Package Installer. CLAW.REZ from your copy
# of the game goes to /data/reclaw/data/CLAW.REZ (FTP or USB); config.xml, saves and
# reclaw.log go to /data/reclaw/save/.
#
# sce_module/: the system libraries (libc.prx, libSceFios2.prx) OpenOrbis
# homebrew carries in its package are NOT in this repository (they are Sony's
# files). Put them in platform/ps4/sce_module/ (e.g. from the OpenOrbis
# samples or your console) before building; without them the package is
# built but may not start.

cmake_minimum_required(VERSION 3.16)
project(ClawPS4 C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/OpenClawGame.cmake")

set(PS4_TITLE_ID "RCLW00002")
set(PS4_TITLE    "ReClaw")
set(PS4_VERSION  "00.10")

# PacBrew's static SDL2, SDL2_image, SDL2_mixer and SDL2_ttf (pkg-config)
openclaw_add_game(openclaw)
openclaw_link_sdl(openclaw NO_GFX)
# Startup steps and performance in save/reclaw.log (first builds on real hardware)
target_compile_definitions(openclaw PRIVATE OPENCLAW_STARTUP_TRACE=1)

add_self(openclaw)

# Package folder: the bundled files, icon, background and system modules.
set(PKGDIR "${CMAKE_BINARY_DIR}/pkg")
openclaw_stage_bundle(openclaw_bundle "${PKGDIR}" "${CMAKE_CURRENT_LIST_DIR}/config.xml")
file(MAKE_DIRECTORY "${PKGDIR}/sce_sys")
configure_file("${CMAKE_CURRENT_LIST_DIR}/sce_sys/icon0.png" "${PKGDIR}/sce_sys/icon0.png" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/sce_sys/pic1.png" "${PKGDIR}/sce_sys/pic1.png" COPYONLY)
file(GLOB SCE_MODULES "${CMAKE_CURRENT_LIST_DIR}/sce_module/*.prx")
if (NOT SCE_MODULES)
    # The OpenOrbis toolchain builds its own libc.prx / libSceFios2.prx (src/modules)
    file(GLOB SCE_MODULES "$ENV{OPENORBIS}/src/modules/*.prx")
endif()
if (EXISTS "$ENV{OPENORBIS}/src/modules/right.sprx")
    file(MAKE_DIRECTORY "${PKGDIR}/sce_sys/about")
    configure_file("$ENV{OPENORBIS}/src/modules/right.sprx" "${PKGDIR}/sce_sys/about/right.sprx" COPYONLY)
endif()
if (SCE_MODULES)
    file(MAKE_DIRECTORY "${PKGDIR}/sce_module")
    foreach(m ${SCE_MODULES})
        get_filename_component(n "${m}" NAME)
        configure_file("${m}" "${PKGDIR}/sce_module/${n}" COPYONLY)
    endforeach()
else()
    message(WARNING "platform/ps4/sce_module/ is empty: the package will lack libc.prx / libSceFios2.prx")
endif()

add_pkg(openclaw "${PKGDIR}" "${PS4_TITLE_ID}" "${PS4_TITLE}" "${PS4_VERSION}")
add_dependencies(openclaw_pkg openclaw_bundle)
