# ReClaw - PlayStation 2 build (ps2dev: ps2toolchain + ps2sdk + gsKit) -> reclaw.elf
#
#   cmake -S platform/ps2 -B build-ps2 -DCMAKE_TOOLCHAIN_FILE=$PS2SDK/ps2dev.cmake
#   cmake --build build-ps2 -j
#
# Needs: the ps2dev/ps2dev image (or ps2dev installed locally) and
# platform/deps/fetch.sh (SDL2 2.32 with its PS2 backend and the add-ons are
# compiled from source with the game). CI: .github/workflows/ports.yml (ps2).
#
# EXPERIMENTAL: the PS2 has 32 MB of RAM; the engine was written for far more.
# Install: reclaw.elf and the bundled files to mass:/reclaw/ (USB) and start it
# with wLaunchELF; CLAW.REZ (+ CLAW.FEC) from your copy of the game goes to
# data/pl/ or data/en/. Emulator: PCSX2 (host: folder or the USB image).

cmake_minimum_required(VERSION 3.16)
project(ReClawPS2 C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/OpenClawGame.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/BundledSDL.cmake")

openclaw_add_game(openclaw)
openclaw_bundled_sdl(openclaw)
target_compile_definitions(openclaw PRIVATE RECLAW_PS2=1 OPENCLAW_STARTUP_TRACE=1 RECLAW_TEXTURE_16BIT=1)

find_package(ZLIB QUIET)
if (ZLIB_FOUND)
    target_link_libraries(openclaw PRIVATE ZLIB::ZLIB)
else()
    target_include_directories(openclaw PRIVATE "$ENV{PS2SDK}/ports/include")
    target_link_libraries(openclaw PRIVATE "$ENV{PS2SDK}/ports/lib/libz.a")
endif()
target_link_libraries(openclaw PRIVATE m)
# std::shared_ptr without a kernel semaphore per object (ps2_atomic.c): the
# kernel has only a few hundred and the game ran out of them in the menu
target_compile_options(openclaw PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-D_GLIBCXX_HAVE_ATOMIC_LOCK_POLICY>)
target_sources(openclaw PRIVATE "${CMAKE_CURRENT_LIST_DIR}/ps2_atomic.c" "${CMAKE_CURRENT_LIST_DIR}/ps2_lock_policy_check.cpp" "${CMAKE_CURRENT_LIST_DIR}/ps2_new.cpp")
set_target_properties(openclaw PROPERTIES OUTPUT_NAME "reclaw" SUFFIX ".elf")
# ps2sdk's default main stack is 128 KB (linkfile: PROVIDE(_stack_size)); the
# engine needs more - building the menu overran it and the game quit without
# an error. 1 MB.
target_link_options(openclaw PRIVATE "-Wl,--defsym,_stack_size=0x100000")

openclaw_stage_bundle(openclaw_bundle "${CMAKE_BINARY_DIR}/reclaw" "${CMAKE_CURRENT_LIST_DIR}/config.xml")
