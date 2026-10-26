# SDL2, SDL2_image, SDL2_mixer and SDL2_ttf built from source together with
# the game (Android, 3DS, Wii U). Sources: platform/deps/fetch.sh.
# Only what the game needs is enabled: WAV sounds,
# PNG/PCX/TGA images through stb_image, TrueType fonts through FreeType.
#
#   include(BundledSDL.cmake)
#   openclaw_bundled_sdl(<target> [SHARED] [SDL_DIR <dir>])
#     SHARED:  shared libraries (Android); otherwise static.
#     SDL_DIR: another SDL2 source tree (the Wii U fork).

set(OPENCLAW_DEPS "${CMAKE_CURRENT_LIST_DIR}/../deps/src")

macro(openclaw_bundled_sdl target)
    cmake_parse_arguments(BSDL "SHARED" "SDL_DIR" "" ${ARGN})
    if (NOT BSDL_SDL_DIR)
        set(BSDL_SDL_DIR "${OPENCLAW_DEPS}/SDL")
    endif()
    foreach(dir "${BSDL_SDL_DIR}" "${OPENCLAW_DEPS}/SDL_image" "${OPENCLAW_DEPS}/SDL_mixer" "${OPENCLAW_DEPS}/SDL_ttf")
        if (NOT EXISTS "${dir}/CMakeLists.txt")
            message(FATAL_ERROR "${dir} is missing: run platform/deps/fetch.sh first")
        endif()
    endforeach()

    # Static builds only compile what the game links; shared ones (Android)
    # build every library so the APK packaging finds them.
    set(_bsdl_exclude EXCLUDE_FROM_ALL)
    if (BSDL_SHARED)
        set(_bsdl_exclude "")
        set(BUILD_SHARED_LIBS ON)
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    else()
        set(BUILD_SHARED_LIBS OFF)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
    endif()
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    add_subdirectory("${BSDL_SDL_DIR}" SDL ${_bsdl_exclude})
    if (NOT BSDL_SHARED AND NOT TARGET SDL2::SDL2)
        add_library(SDL2::SDL2 ALIAS SDL2-static)
    endif()

    set(SDL2IMAGE_VENDORED ON CACHE BOOL "" FORCE)
    set(SDL2IMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
    set(SDL2IMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
    foreach(opt AVIF JXL TIF WEBP JPG_SHARED PNG_SHARED SAMPLES TESTS INSTALL)
        set(SDL2IMAGE_${opt} OFF CACHE BOOL "" FORCE)
    endforeach()
    add_subdirectory("${OPENCLAW_DEPS}/SDL_image" SDL_image ${_bsdl_exclude})

    set(SDL2MIXER_VENDORED ON CACHE BOOL "" FORCE)
    set(SDL2MIXER_DEPS_SHARED OFF CACHE BOOL "" FORCE)
    foreach(opt FLAC MOD MP3 MIDI OPUS WAVPACK GME SAMPLES INSTALL)
        set(SDL2MIXER_${opt} OFF CACHE BOOL "" FORCE)
    endforeach()
    # OFF: the game's own stb_vorbis (TinySoundFont) would clash with SDL_mixer's.
    set(SDL2MIXER_VORBIS "OFF" CACHE STRING "" FORCE)
    add_subdirectory("${OPENCLAW_DEPS}/SDL_mixer" SDL_mixer ${_bsdl_exclude})

    set(SDL2TTF_VENDORED ON CACHE BOOL "" FORCE)
    set(SDL2TTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
    set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL2TTF_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory("${OPENCLAW_DEPS}/SDL_ttf" SDL_ttf ${_bsdl_exclude})

    # The engine includes <SDL2/SDL_image.h> etc.: forwarding headers for the
    # add-ons (SDL's own build already exposes include/SDL2/).
    set(_shim "${CMAKE_BINARY_DIR}/sdl-include")
    file(MAKE_DIRECTORY "${_shim}/SDL2")
    foreach(h "${OPENCLAW_DEPS}/SDL_image/include/SDL_image.h"
              "${OPENCLAW_DEPS}/SDL_mixer/include/SDL_mixer.h"
              "${OPENCLAW_DEPS}/SDL_ttf/SDL_ttf.h")
        get_filename_component(_name "${h}" NAME)
        file(WRITE "${_shim}/SDL2/${_name}" "#include \"${h}\"\n")
    endforeach()
    target_include_directories(${target} BEFORE PRIVATE "${_shim}")
    target_compile_definitions(${target} PRIVATE OPENCLAW_NO_SDL2_GFX=1)
    foreach(_lib SDL2_image SDL2_mixer SDL2_ttf)
        if (TARGET ${_lib}::${_lib})
            target_link_libraries(${target} PRIVATE ${_lib}::${_lib})
        else()
            target_link_libraries(${target} PRIVATE ${_lib}::${_lib}-static)
        endif()
    endforeach()
    target_link_libraries(${target} PRIVATE SDL2::SDL2)
    if (TARGET SDL2::SDL2main)
        target_link_libraries(${target} PRIVATE SDL2::SDL2main)
    endif()
endmacro()
