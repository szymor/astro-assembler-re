# Cross-compiling for 64-bit Windows with MinGW-w64 from Linux.
# Expects the toolchain and Windows builds of SDL3 and zlib under
# MINGW_ROOT (default /usr/x86_64-w64-mingw32), as distributions install them.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_PREFIX x86_64-w64-mingw32)
if(NOT DEFINED MINGW_ROOT)
    set(MINGW_ROOT /usr/${MINGW_PREFIX})
endif()

set(CMAKE_C_COMPILER ${MINGW_PREFIX}-gcc)
set(CMAKE_RC_COMPILER ${MINGW_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH ${MINGW_ROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
