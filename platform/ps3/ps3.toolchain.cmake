# CMake toolchain for the PS3 (ps3toolchain + PSL1GHT).
# PS3DEV must point to the toolchain, PSL1GHT to the SDK (often the same).

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ppc64)

if (NOT DEFINED ENV{PS3DEV})
    message(FATAL_ERROR "Please set PS3DEV (export PS3DEV=/usr/local/ps3dev)")
endif()
set(PS3DEV "$ENV{PS3DEV}")
if (DEFINED ENV{PSL1GHT})
    set(PSL1GHT "$ENV{PSL1GHT}")
else()
    set(PSL1GHT "${PS3DEV}")
endif()

set(CMAKE_C_COMPILER   "${PS3DEV}/ppu/bin/powerpc64-ps3-elf-gcc")
set(CMAKE_CXX_COMPILER "${PS3DEV}/ppu/bin/powerpc64-ps3-elf-g++")
set(CMAKE_AR           "${PS3DEV}/ppu/bin/powerpc64-ps3-elf-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB       "${PS3DEV}/ppu/bin/powerpc64-ps3-elf-ranlib" CACHE FILEPATH "")
set(CMAKE_STRIP        "${PS3DEV}/ppu/bin/powerpc64-ps3-elf-strip" CACHE FILEPATH "")

set(CMAKE_FIND_ROOT_PATH "${PSL1GHT}/ppu" "${PS3DEV}/ppu" "${PS3DEV}/portlibs/ppu")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(PS3_FLAGS "-mcpu=cell -mhard-float -fmodulo-sched -ffunction-sections -fdata-sections -D__PSL1GHT__ -D__PS3__")
set(CMAKE_C_FLAGS_INIT   "${PS3_FLAGS} -I${PSL1GHT}/ppu/include -I${PS3DEV}/portlibs/ppu/include")
set(CMAKE_CXX_FLAGS_INIT "${PS3_FLAGS} -I${PSL1GHT}/ppu/include -I${PS3DEV}/portlibs/ppu/include")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-L${PSL1GHT}/ppu/lib -L${PS3DEV}/portlibs/ppu/lib -Wl,--gc-sections")

# pkg-config files of the portlibs (SDL2, SDL2_mixer, SDL2_image, SDL2_ttf...)
set(ENV{PKG_CONFIG_LIBDIR} "${PS3DEV}/portlibs/ppu/lib/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
