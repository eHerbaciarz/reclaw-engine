# Android: the game as libmain.so, loaded by SDL's Java activity
# (org.openclaw.claw.ClawActivity). SDL2 and its add-ons are built from
# source as shared libraries (platform/android/fetch-deps.sh fetches them).

cmake_minimum_required(VERSION 3.16)
project(ClawAndroid C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/OpenClawGame.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/BundledSDL.cmake")

openclaw_add_game(main LIBRARY)
openclaw_bundled_sdl(main SHARED)
target_link_libraries(main PRIVATE z)
if (ANDROID)
    target_link_libraries(main PRIVATE log android)
endif()
