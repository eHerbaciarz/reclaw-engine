# Shared part of every native build (desktop and consoles): which sources make
# up the game, include paths and compiler flags. Each platform's CMakeLists.txt
# includes this file, calls openclaw_add_game() and then adds its own
# libraries and packaging.
#
# Keep the source selection in sync with platform/switch/Makefile.

get_filename_component(OPENCLAW_REPO "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

function(openclaw_sources out_var)
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS
        "${OPENCLAW_REPO}/OpenClaw/*.cpp"
        "${OPENCLAW_REPO}/libwap/*.cpp"
        "${OPENCLAW_REPO}/Box2D/Box2D/*.cpp"
        "${OPENCLAW_REPO}/ThirdParty/Tinyxml/*.cpp")
    list(FILTER sources EXCLUDE REGEX "/WASM/")                                  # browser-only backends
    list(FILTER sources EXCLUDE REGEX "/OpenClaw/Engine/Graphics/[^/]+\\.cpp$")  # WebGL graphics layer
    list(FILTER sources EXCLUDE REGEX "/OpenClaw/Engine/Graphics/(Generic|WebGL|WebGPU)/")
    list(FILTER sources EXCLUDE REGEX "/Audio/WebAudioAPI\\.cpp$")
    list(FILTER sources EXCLUDE REGEX "/libwap/Miniz\\.cpp$")                    # same miniz as Engine/Resource
    list(FILTER sources EXCLUDE REGEX "/Tinyxml/xmltest\\.cpp$")
    # libsmacker (LGPL-2.1, C): decoder for the original Smacker cutscenes
    list(APPEND sources "${OPENCLAW_REPO}/ThirdParty/libsmacker/smacker.c")
    set(${out_var} ${sources} PARENT_SCOPE)
endfunction()

# openclaw_add_game(<target> [LIBRARY])
#   LIBRARY: build a shared library instead of an executable (Android).
function(openclaw_add_game target)
    cmake_parse_arguments(ARG "LIBRARY" "" "" ${ARGN})
    openclaw_sources(sources)
    if (ARG_LIBRARY)
        add_library(${target} SHARED ${sources})
    else()
        add_executable(${target} ${sources})
    endif()

    target_include_directories(${target} PRIVATE
        "${OPENCLAW_REPO}/OpenClaw"
        "${OPENCLAW_REPO}/Box2D"
        "${OPENCLAW_REPO}/libwap"
        "${OPENCLAW_REPO}/ThirdParty/Tinyxml")
    # ThirdParty/ also bundles SDL 2.0.4 headers for the old Windows build;
    # searched last so <SDL2/...> matches the SDL that is linked.
    target_compile_options(${target} PRIVATE "-idirafter" "${OPENCLAW_REPO}/ThirdParty")
    target_compile_definitions(${target} PRIVATE OPENCLAW_NATIVE=1)
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
        -Wno-narrowing -w)
endfunction()

# openclaw_link_sdl(<target> [NO_GFX])
#   Links SDL2 and its add-on libraries through pkg-config, statically
#   (consoles have no shared libraries). NO_GFX: the platform has no
#   SDL2_gfx (only the physics debug view used it).
function(openclaw_link_sdl target)
    cmake_parse_arguments(ARG "NO_GFX" "" "" ${ARGN})
    find_package(PkgConfig REQUIRED)
    set(modules SDL2_mixer SDL2_image SDL2_ttf)
    if (NOT ARG_NO_GFX)
        list(APPEND modules SDL2_gfx)
    else()
        target_compile_definitions(${target} PRIVATE OPENCLAW_NO_SDL2_GFX=1)
    endif()
    list(APPEND modules sdl2 zlib)
    pkg_check_modules(OPENCLAW_SDL REQUIRED ${modules})
    target_include_directories(${target} PRIVATE ${OPENCLAW_SDL_STATIC_INCLUDE_DIRS})
    target_compile_options(${target} PRIVATE ${OPENCLAW_SDL_STATIC_CFLAGS_OTHER})
    target_link_libraries(${target} PRIVATE ${OPENCLAW_SDL_STATIC_LDFLAGS})
endfunction()

# Copies the files that ship with the port (no game data) into <dir>:
#   config.xml ASSETS.ZIP clacon.ttf console02.tga splash.ttf music/soundfont.sf3
# as build outputs (packaging steps can depend on the files), plus the target
# <name> that builds them.
function(openclaw_stage_bundle name dir config)
    file(GLOB_RECURSE assets CONFIGURE_DEPENDS "${OPENCLAW_REPO}/Build_Release/ASSETS/*")
    set(outputs
        "${dir}/config.xml" "${dir}/ASSETS.ZIP" "${dir}/clacon.ttf" "${dir}/console02.tga"
        "${dir}/splash.ttf" "${dir}/subtitles.ttf" "${dir}/music/soundfont.sf3")
    add_custom_command(OUTPUT ${outputs}
        COMMAND bash "${OPENCLAW_REPO}/scripts/stage_bundle.sh" "${dir}" "${config}"
        DEPENDS ${assets} "${config}" "${OPENCLAW_REPO}/scripts/stage_bundle.sh"
        COMMENT "Staging the bundled files in ${dir}"
        VERBATIM)
    add_custom_target(${name} ALL DEPENDS ${outputs})
    set(OPENCLAW_BUNDLE_FILES ${outputs} PARENT_SCOPE)
endfunction()
