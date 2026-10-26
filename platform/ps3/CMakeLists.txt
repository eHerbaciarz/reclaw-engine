# PlayStation 3 build (ps3toolchain + PSL1GHT + SDL2 PSL1GHT port)
#   -> claw.pkg, claw.self, EBOOT.BIN
#
#   export PS3DEV=/usr/local/ps3dev PSL1GHT=$PS3DEV
#   cmake -S platform/ps3 -B build-ps3 -DCMAKE_TOOLCHAIN_FILE=platform/ps3/ps3.toolchain.cmake
#   cmake --build build-ps3 -j
#
# Needs in $PS3DEV/portlibs/ppu: SDL2 (ps3libraries 014-sdl2_psl1ght),
# SDL2_mixer (016), zlib, libpng, freetype, plus SDL2_image and SDL2_ttf built
# from source (platform/ps3/build-sdl-addons.sh). CI: .github/workflows/ps3.yml
#
# Install: claw.pkg (Package Manager on HEN/CFW) or the .self in RPCS3.
# CLAW.REZ from your copy of the game goes to
# /dev_hdd0/game/CLAW00001/USRDIR/CLAW.REZ (config, saves and log too).

cmake_minimum_required(VERSION 3.16)
project(ClawPS3 C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/OpenClawGame.cmake")

set(PS3_APPID     "CLAW00001")
set(PS3_CONTENTID "UP0001-${PS3_APPID}_00-0000000000000000")
set(PS3_TITLE     "Captain Claw")

openclaw_add_game(openclaw)
openclaw_link_sdl(openclaw NO_GFX)
# PSL1GHT system libraries used by SDL and the network race
target_link_libraries(openclaw PRIVATE
    rsx gcm_sys io audio sysutil sysmodule net netctl rt lv2 m)
set_target_properties(openclaw PROPERTIES OUTPUT_NAME "claw" SUFFIX ".elf")

set(PKGDIR "${CMAKE_BINARY_DIR}/pkg")
openclaw_stage_bundle(openclaw_bundle "${PKGDIR}/USRDIR" "${CMAKE_CURRENT_LIST_DIR}/config.xml")

set(ELF "$<TARGET_FILE:openclaw>")
set(STRIPPED "${CMAKE_BINARY_DIR}/claw_stripped.elf")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/claw.pkg" "${CMAKE_BINARY_DIR}/claw.self"
    # unsigned SELF for RPCS3 / CFW
    COMMAND ${CMAKE_STRIP} "${ELF}" -o "${STRIPPED}"
    COMMAND sprxlinker "${STRIPPED}"
    COMMAND make_self "${STRIPPED}" "${CMAKE_BINARY_DIR}/claw.self"
    # NPDRM EBOOT + package
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PKGDIR}/USRDIR"
    COMMAND make_self_npdrm "${STRIPPED}" "${PKGDIR}/USRDIR/EBOOT.BIN" "${PS3_CONTENTID}"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_LIST_DIR}/ICON0.PNG" "${PKGDIR}/ICON0.PNG"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_LIST_DIR}/PIC1.PNG" "${PKGDIR}/PIC1.PNG"
    COMMAND sfo.py --title "${PS3_TITLE}" --appid "${PS3_APPID}" -f "${CMAKE_CURRENT_LIST_DIR}/sfo.xml" "${PKGDIR}/PARAM.SFO"
    COMMAND pkg.py --contentid "${PS3_CONTENTID}" "${PKGDIR}/" "${CMAKE_BINARY_DIR}/claw.pkg"
    DEPENDS openclaw ${OPENCLAW_BUNDLE_FILES} sfo.xml ICON0.PNG PIC1.PNG
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Packaging claw.pkg / claw.self"
    VERBATIM)
add_custom_target(claw_pkg ALL DEPENDS "${CMAKE_BINARY_DIR}/claw.pkg")
