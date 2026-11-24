cmake_minimum_required(VERSION 3.21)
project(astro_assembler C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Generated C of the recompiled assembly core (re/recomp/recomp.py). It is
# derived from ASTRO.exe and therefore built locally, never committed.
set(ASTRO_GEN_DIR "${CMAKE_SOURCE_DIR}/build/recomp/gen" CACHE PATH "Directory with the generated core (gen_*.c)")

# Frontends: one executable each. SDL2/SDL1 are built when found.
option(ASTRO_SDL3 "Build the SDL3 frontend (astro)" ON)
option(ASTRO_SDL2 "Build the SDL2 frontend (astro-sdl2) if SDL2 is found" ON)
option(ASTRO_SDL1 "Build the SDL 1.2 frontend (astro-sdl1) if SDL 1.2 is found" ON)
option(ASTRO_HEADLESS "Build the headless test frontend (astro-headless)" ON)

file(GLOB ASTRO_GEN_SOURCES "${ASTRO_GEN_DIR}/gen_*.c")
if(NOT ASTRO_GEN_SOURCES)
    message(FATAL_ERROR "No generated core found in ${ASTRO_GEN_DIR}. See docs/recompiler.md.")
endif()

if(MINGW)
    set(ZLIB_USE_STATIC_LIBS ON)                  # one DLL less to ship
    add_link_options(-static-libgcc)
    # Some MinGW toolchains enable these by default, which needs libssp-0.dll.
    add_compile_options(-fno-stack-protector -U_FORTIFY_SOURCE)
endif()
find_package(ZLIB REQUIRED)                       # PNG images

if(MSVC)
    set(ASTRO_WARNINGS /W3 /wd4244 /wd4267 /wd4996)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    set(ASTRO_WARNINGS -Wall -Wextra -Wno-unused-parameter)
endif()

# The generated code is huge and mechanical: its own library, moderate
# optimisation, no warnings.
add_library(astro_core STATIC ${ASTRO_GEN_SOURCES} re/recomp/recomp_rt.c)
target_include_directories(astro_core PUBLIC re/recomp "${ASTRO_GEN_DIR}")
if(MSVC)
    target_compile_options(astro_core PRIVATE /O1 /w /bigobj)
else()
    target_compile_options(astro_core PRIVATE -O1 -w)
    target_link_libraries(astro_core PUBLIC m)
endif()

# Frontend-neutral code (the replacement for the original shell), compiled once
# and linked into every frontend.
add_library(astro_common OBJECT
    src/game.c
    src/shell.c
    src/callbacks.c
    src/video.c
    src/input.c
    src/assets.c
    src/audio.c
    src/config.c
    src/settings.c
    src/util.c
    src/png.c
    src/scale.c
)
target_include_directories(astro_common PUBLIC src third_party/dr_flac)
target_link_libraries(astro_common PUBLIC astro_core ZLIB::ZLIB)
target_compile_options(astro_common PRIVATE ${ASTRO_WARNINGS})

function(astro_frontend name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE astro_common ${ARGN})
    target_compile_options(${name} PRIVATE ${ASTRO_WARNINGS})
endfunction()

if(ASTRO_SDL3)
    find_package(SDL3 REQUIRED CONFIG)
    astro_frontend(astro src/frontend/sdl3.c SDL3::SDL3)
endif()

if(ASTRO_SDL2)
    find_package(SDL2 CONFIG QUIET)
    if(TARGET SDL2::SDL2)
        astro_frontend(astro-sdl2 src/frontend/sdl2.c SDL2::SDL2)
    else()
        message(STATUS "SDL2 not found: astro-sdl2 not built")
    endif()
endif()

if(ASTRO_SDL1)
    find_package(SDL QUIET)                       # CMake's FindSDL (SDL 1.2 / sdl12-compat)
    if(SDL_FOUND)
        astro_frontend(astro-sdl1 src/frontend/sdl1.c ${SDL_LIBRARY})
        target_include_directories(astro-sdl1 PRIVATE ${SDL_INCLUDE_DIR})
    else()
        message(STATUS "SDL 1.2 not found: astro-sdl1 not built")
    endif()
endif()

if(ASTRO_HEADLESS)
    astro_frontend(astro-headless src/frontend/headless.c)
endif()

# Windows: put the runtime DLLs next to the executables. Some MinGW
# distributions build SDL itself with the stack protector, so its DLL needs
# libssp-0.dll from the toolchain; copy it when the toolchain has one.
if(WIN32)
    if(MINGW)
        find_file(ASTRO_MINGW_SSP libssp-0.dll PATHS ${CMAKE_FIND_ROOT_PATH} PATH_SUFFIXES bin NO_DEFAULT_PATH)
    endif()
    foreach(t astro astro-sdl2)
        if(TARGET ${t})
            add_custom_command(TARGET ${t} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:${t}> $<TARGET_FILE_DIR:${t}>
                COMMAND_EXPAND_LISTS)
            if(ASTRO_MINGW_SSP)
                add_custom_command(TARGET ${t} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ASTRO_MINGW_SSP} $<TARGET_FILE_DIR:${t}>)
            endif()
        endif()
    endforeach()
endif()
