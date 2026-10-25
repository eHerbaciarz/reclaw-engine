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

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2LIBS REQUIRED IMPORTED_TARGET sdl2 SDL2_image SDL2_mixer SDL2_ttf SDL2_gfx zlib)
find_package(Threads REQUIRED)

# --- Source selection (keep in sync with platform/switch/Makefile) -----------
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${REPO_ROOT}/OpenClaw/*.cpp"
    "${REPO_ROOT}/libwap/*.cpp"
    "${REPO_ROOT}/Box2D/Box2D/*.cpp"
    "${REPO_ROOT}/ThirdParty/Tinyxml/*.cpp")

list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/WASM/")                         # browser-only backends
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/OpenClaw/Engine/Graphics/[^/]+\\.cpp$") # WebGL graphics layer
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/OpenClaw/Engine/Graphics/(Generic|WebGL|WebGPU)/")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/Audio/WebAudioAPI\\.cpp$")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/libwap/Miniz\\.cpp$")           # same miniz as Engine/Resource
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/Tinyxml/xmltest\\.cpp$")

# libsmacker (LGPL-2.1, C): decoder for the original Smacker cutscenes
list(APPEND ENGINE_SOURCES "${REPO_ROOT}/ThirdParty/libsmacker/smacker.c")

add_executable(openclaw ${ENGINE_SOURCES})

target_include_directories(openclaw PRIVATE
    "${REPO_ROOT}/OpenClaw"
    "${REPO_ROOT}/Box2D"
    "${REPO_ROOT}/libwap"
    "${REPO_ROOT}/ThirdParty/Tinyxml")
# ThirdParty/ also bundles SDL 2.0.4 headers for the old Windows build; search
# it after the system directories so <SDL2/...> matches the linked SDL.
target_compile_options(openclaw PRIVATE "-idirafter" "${REPO_ROOT}/ThirdParty")

target_compile_definitions(openclaw PRIVATE OPENCLAW_NATIVE=1)
target_compile_options(openclaw PRIVATE -fpermissive -Wno-narrowing)
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
