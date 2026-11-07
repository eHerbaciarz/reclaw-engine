# PlayStation 3 build (ps3toolchain + PSL1GHT + SDL2 PSL1GHT port)
#   -> reclaw.pkg, reclaw.self, EBOOT.BIN
#
#   export PS3DEV=/usr/local/ps3dev PSL1GHT=$PS3DEV
#   cmake -S platform/ps3 -B build-ps3 -DCMAKE_TOOLCHAIN_FILE=platform/ps3/ps3.toolchain.cmake
#   cmake --build build-ps3 -j
#
# Needs in $PS3DEV/portlibs/ppu: SDL2 (ps3libraries 014-sdl2_psl1ght),
# SDL2_mixer (016), zlib, libpng, freetype, plus SDL2_image and SDL2_ttf built
# from source (platform/ps3/build-sdl-addons.sh). CI: .github/workflows/ps3.yml
# SDL2 with platform/deps/patches/SDL-psl1ght-audio.patch (the audio blocks
# written ahead of the hardware's read position; without it the sound crackles).
#
# Install: reclaw_hen.pkg (finalized, Package Manager on HEN/CFW), reclaw.pkg
# (debug package) or the .self in RPCS3. The first run creates the folders in
# /dev_hdd0/game/RCLW00001/USRDIR/: CLAW.REZ and CLAW.FEC from your copy of the
# game go to data/pl/ (Polish) or data/en/ (English); saves and log in save/.

cmake_minimum_required(VERSION 3.16)
project(ClawPS3 C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/OpenClawGame.cmake")

set(PS3_APPID     "RCLW00001")
set(PS3_CONTENTID "UP0001-${PS3_APPID}_00-0000000000000000")
set(PS3_TITLE     "ReClaw")

openclaw_add_game(openclaw)
# the game draws in software, the PSL1GHT renderer only shows the frame
target_sources(openclaw PRIVATE "${CMAKE_CURRENT_LIST_DIR}/compat/ps3_sdl_screen.cpp")
target_include_directories(openclaw BEFORE PRIVATE "${CMAKE_CURRENT_LIST_DIR}/compat")
# Startup steps and performance in save/reclaw.log (first builds on real hardware)
target_compile_definitions(openclaw PRIVATE OPENCLAW_STARTUP_TRACE=1)
target_compile_options(openclaw PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${CMAKE_CURRENT_LIST_DIR}/compat/ps3_cxx_compat.h>")
# Drawing with the RSX (gpu/ps3_gpu.cpp; port_settings.txt gpu=0: software):
# every SDL render call of the game goes through gpu/ps3_gpu.h
target_sources(openclaw PRIVATE "${CMAKE_CURRENT_LIST_DIR}/gpu/ps3_gpu.cpp")
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/gpu/ps3_gpu.cpp" "${CMAKE_CURRENT_LIST_DIR}/compat/ps3_sdl_screen.cpp"
    PROPERTIES COMPILE_DEFINITIONS RECLAW_GPU_IMPL)
target_compile_definitions(openclaw PRIVATE RECLAW_PS3_GPU RECLAW_GPU)
target_compile_options(openclaw PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${CMAKE_CURRENT_LIST_DIR}/gpu/ps3_gpu.h>")
openclaw_link_sdl(openclaw NO_GFX)
# PSL1GHT system libraries used by SDL and the network race
target_link_libraries(openclaw PRIVATE
    rsx gcm_sys io audio net netctl sysutil sysmodule rt lv2 m)
set_target_properties(openclaw PROPERTIES OUTPUT_NAME "reclaw" SUFFIX ".elf")
# Our copy of PSL1GHT's linker script (gcc's -T lv2.ld%s finds it first):
# libstdc++'s stream set-up must run after PSL1GHT's own (ld/lv2.ld)
target_link_options(openclaw PRIVATE "-B${CMAKE_CURRENT_LIST_DIR}/ld/")

set(PKGDIR "${CMAKE_BINARY_DIR}/pkg")
openclaw_stage_bundle(openclaw_bundle "${PKGDIR}/USRDIR" "${CMAKE_CURRENT_LIST_DIR}/config.xml")

# Packaging tools: current PSL1GHT has C tools (sfo, pkg), older ones the .py scripts
find_program(PS3_SFO NAMES sfo sfo.py HINTS "$ENV{PS3DEV}/bin" REQUIRED)
find_program(PS3_PKG NAMES pkg pkg.py HINTS "$ENV{PS3DEV}/bin" REQUIRED)
find_program(PS3_FINALIZE NAMES package_finalize HINTS "$ENV{PS3DEV}/bin")

set(ELF "$<TARGET_FILE:openclaw>")
set(STRIPPED "${CMAKE_BINARY_DIR}/reclaw_stripped.elf")
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/reclaw.pkg" "${CMAKE_BINARY_DIR}/reclaw.self"
    # unsigned SELF for RPCS3 / CFW
    COMMAND ${CMAKE_STRIP} "${ELF}" -o "${STRIPPED}"
    COMMAND sprxlinker "${STRIPPED}"
    COMMAND make_self "${STRIPPED}" "${CMAKE_BINARY_DIR}/reclaw.self"
    # NPDRM EBOOT + package
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PKGDIR}/USRDIR"
    COMMAND make_self_npdrm "${STRIPPED}" "${PKGDIR}/USRDIR/EBOOT.BIN" "${PS3_CONTENTID}"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_LIST_DIR}/ICON0.PNG" "${PKGDIR}/ICON0.PNG"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_LIST_DIR}/PIC1.PNG" "${PKGDIR}/PIC1.PNG"
    COMMAND ${PS3_SFO} --title "${PS3_TITLE}" --appid "${PS3_APPID}" -f "${CMAKE_CURRENT_LIST_DIR}/sfo.xml" "${PKGDIR}/PARAM.SFO"
    COMMAND ${PS3_PKG} --contentid "${PS3_CONTENTID}" "${PKGDIR}/" "${CMAKE_BINARY_DIR}/reclaw.pkg"
    # retail-signed copy for HEN (installs from Package Manager without debug settings)
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/reclaw.pkg" "${CMAKE_BINARY_DIR}/reclaw_hen.pkg"
    COMMAND ${PS3_FINALIZE} "${CMAKE_BINARY_DIR}/reclaw_hen.pkg"
    DEPENDS openclaw ${OPENCLAW_BUNDLE_FILES} sfo.xml ICON0.PNG PIC1.PNG
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Packaging reclaw.pkg / reclaw.self"
    VERBATIM)
add_custom_target(reclaw_pkg ALL DEPENDS "${CMAKE_BINARY_DIR}/reclaw.pkg")
