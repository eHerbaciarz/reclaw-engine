# Windows build (cross-compiled with MinGW-w64), SDL2 and its add-ons built
# from source and linked statically (platform/deps/fetch.sh):
#   cmake -S platform/windows -B build-windows \
#         -DCMAKE_TOOLCHAIN_FILE=platform/windows/mingw-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-windows -j
# -> ReClaw.exe (with the ReClaw icon) that needs no DLLs. Packaging:
# platform/windows/package.sh. (build.sh / build-deps.sh: the older build
# with SDL2 DLLs.)
cmake_minimum_required(VERSION 3.16)
project(ReClawWindows C CXX RC)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/OpenClawGame.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/BundledSDL.cmake")

openclaw_add_game(openclaw)
openclaw_bundled_sdl(openclaw)
target_sources(openclaw PRIVATE "${CMAKE_CURRENT_LIST_DIR}/reclaw.rc")
target_link_libraries(openclaw PRIVATE ws2_32 iphlpapi)
target_link_options(openclaw PRIVATE -static -static-libgcc -static-libstdc++ -mwindows)
set_target_properties(openclaw PROPERTIES OUTPUT_NAME "ReClaw")
