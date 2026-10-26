# Native desktop build (Linux) of the merged engine.
#
# The repository's root CMakeLists.txt is the Emscripten/WebAssembly build and is
# left untouched. This one builds the same game code natively against system
# SDL2 — it is also the quickest way to test changes meant for the Nintendo
# Switch port (platform/switch), which uses the identical source selection.
#
#   cmake -S platform/linux -B build-linux -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-linux -j
#   cd Build_Release && ../build-linux/openclaw
#
# Requires: SDL2, SDL2_image, SDL2_mixer, SDL2_ttf, SDL2_gfx, zlib (pkg-config).

cmake_minimum_required(VERSION 3.16)
project(OpenClawNative CXX C)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/OpenClawGame.cmake")

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2LIBS REQUIRED IMPORTED_TARGET sdl2 SDL2_image SDL2_mixer SDL2_ttf SDL2_gfx zlib)
find_package(Threads REQUIRED)

openclaw_add_game(openclaw)
target_link_libraries(openclaw PRIVATE PkgConfig::SDL2LIBS Threads::Threads)

# Windows (MinGW-w64 cross build, see platform/windows/build.sh): sockets for
# the network race, no runtime DLLs of the compiler.
if (WIN32)
    target_link_libraries(openclaw PRIVATE ws2_32 iphlpapi)
    # SDL2_image is a static library here: its PNG support needs libpng/zlib
    # after it on the link line.
    set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES} -L${MINGW_PREFIX}/lib -lpng16 -lz")
    target_link_options(openclaw PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
endif()
