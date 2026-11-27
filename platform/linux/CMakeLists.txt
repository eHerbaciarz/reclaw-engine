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
pkg_check_modules(SDL2LIBS REQUIRED IMPORTED_TARGET sdl2 SDL2_image SDL2_mixer SDL2_ttf zlib)
# SDL2_gfx (only the physics debug view uses it) is optional: its pkg-config
# name differs between distributions and Homebrew.
pkg_search_module(SDL2GFX QUIET IMPORTED_TARGET SDL2_gfx sdl2_gfx SDL2_gfx-1.0)
find_package(Threads REQUIRED)

openclaw_add_game(openclaw)
target_link_libraries(openclaw PRIVATE PkgConfig::SDL2LIBS Threads::Threads)
# The engine includes <SDL2/SDL.h>: the parents of pkg-config's .../include/SDL2
# (on macOS /opt/homebrew/include is not searched by default, and the old
# SDL 2.0.4 headers in ThirdParty/ would be picked up instead).
message(STATUS "SDL2 include dirs: ${SDL2LIBS_INCLUDE_DIRS}")
foreach(_dir ${SDL2LIBS_INCLUDE_DIRS})
    if (_dir MATCHES "/SDL2$")
        get_filename_component(_parent "${_dir}" DIRECTORY)
        target_include_directories(openclaw BEFORE PRIVATE "${_parent}")
    endif()
endforeach()
if (SDL2GFX_FOUND)
    target_link_libraries(openclaw PRIVATE PkgConfig::SDL2GFX)
else()
    message(STATUS "SDL2_gfx not found: building without the physics debug view")
    target_compile_definitions(openclaw PRIVATE OPENCLAW_NO_SDL2_GFX=1)
endif()

# Windows (MinGW-w64 cross build, see platform/windows/build.sh): sockets for
# the network race, no runtime DLLs of the compiler.
if (WIN32)
    target_link_libraries(openclaw PRIVATE ws2_32 iphlpapi)
    # SDL2_image is a static library here: its PNG support needs libpng/zlib
    # after it on the link line.
    set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_CXX_STANDARD_LIBRARIES} -L${MINGW_PREFIX}/lib -lpng16 -lz")
    target_link_options(openclaw PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
endif()
