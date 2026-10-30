# Cross build with zig cc for Linux handhelds (PortMaster, aarch64) and the
# Steam Deck (x86_64), against an old glibc. Set ZIG_TARGET in the environment
# (aarch64-linux-gnu.2.27 / x86_64-linux-gnu.2.31) and RECLAW_DEPS to the
# prefix made by build-deps.sh.
set(CMAKE_SYSTEM_NAME Linux)
string(REGEX MATCH "^[^-]+" CMAKE_SYSTEM_PROCESSOR "$ENV{ZIG_TARGET}")
set(_d "${CMAKE_CURRENT_LIST_DIR}")
set(CMAKE_C_COMPILER "${_d}/zig-cc")
set(CMAKE_CXX_COMPILER "${_d}/zig-c++")
set(CMAKE_AR "${_d}/zig-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${_d}/zig-ranlib" CACHE FILEPATH "")
set(CMAKE_FIND_ROOT_PATH "$ENV{RECLAW_DEPS}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(ENV{PKG_CONFIG_LIBDIR} "$ENV{RECLAW_DEPS}/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
