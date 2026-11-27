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
option(ASTRO_BOT "Build astro-bot, which plays the game by search (POSIX only)" ON)

option(ASTRO_DIFF "Differential build: check every hand-written core function against its generated version on each call (slow)" OFF)
option(ASTRO_COVERAGE "Line and branch coverage (gcov) of the hand-written core functions (GCC/Clang)" OFF)
option(ASTRO_PROFILE "Count the calls of every generated core function (ASTRO_PROFILE_OUT=<file>)" OFF)

# Functions rewritten by hand replace generated ones (src/core/replaced.h).
# With Ghidra's p-code export at hand (docs/recompiler.md, step 2), the core
# is regenerated whenever that list or the recompiler changes; otherwise the
# existing generated files are used, and the game refuses to start if they do
# not match the list.
set(ASTRO_PCODE "${CMAKE_SOURCE_DIR}/build/recomp/core.pcode" CACHE FILEPATH "Ghidra p-code export of the core (ExportPcode.java)")
set(ASTRO_ENTRIES "${CMAKE_SOURCE_DIR}/build/recomp/entries.txt" CACHE FILEPATH "Extra core entry points (find_seeds.py)")
set(ASTRO_TEXT_IMAGE "${CMAKE_SOURCE_DIR}/assets/data/ASTRO_TEXT.BIN" CACHE FILEPATH "Code section image (package_assets.py)")
if(EXISTS "${ASTRO_PCODE}" AND EXISTS "${ASTRO_ENTRIES}" AND EXISTS "${ASTRO_TEXT_IMAGE}")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(ASTRO_GEN_SOURCES "${ASTRO_GEN_DIR}/gen_table.c")
    foreach(i RANGE 0 31)
        string(LENGTH "${i}" len)
        if(len EQUAL 1)
            set(i "0${i}")
        endif()
        list(APPEND ASTRO_GEN_SOURCES "${ASTRO_GEN_DIR}/gen_${i}.c")
    endforeach()
    # The recompiler rewrites only files whose content changed, so a stamp
    # (per build directory) marks the last run.
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/recomp.stamp"
        BYPRODUCTS ${ASTRO_GEN_SOURCES} "${ASTRO_GEN_DIR}/gen_funcs.h"
        COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/re/recomp/recomp.py" "${ASTRO_PCODE}"
                -o "${ASTRO_GEN_DIR}" --entries "${ASTRO_ENTRIES}"
                --smc "${CMAKE_SOURCE_DIR}/re/smc_fields.txt" --tables "${CMAKE_SOURCE_DIR}/re/inline_tables.txt"
                --text "${ASTRO_TEXT_IMAGE}" --replaced "${CMAKE_SOURCE_DIR}/src/core/replaced.h"
        COMMAND "${CMAKE_COMMAND}" -E touch "${CMAKE_CURRENT_BINARY_DIR}/recomp.stamp"
        DEPENDS "${CMAKE_SOURCE_DIR}/re/recomp/recomp.py" "${CMAKE_SOURCE_DIR}/src/core/replaced.h"
                "${ASTRO_PCODE}" "${ASTRO_ENTRIES}" "${ASTRO_TEXT_IMAGE}"
                "${CMAKE_SOURCE_DIR}/re/smc_fields.txt" "${CMAKE_SOURCE_DIR}/re/inline_tables.txt"
        COMMENT "Regenerating the recompiled core"
        VERBATIM)
    set(ASTRO_GEN_STAMP "${CMAKE_CURRENT_BINARY_DIR}/recomp.stamp")
else()
    file(GLOB ASTRO_GEN_SOURCES "${ASTRO_GEN_DIR}/gen_*.c")
    if(NOT ASTRO_GEN_SOURCES)
        message(FATAL_ERROR "No generated core found in ${ASTRO_GEN_DIR}. See docs/recompiler.md.")
    endif()
    list(APPEND ASTRO_GEN_SOURCES "${ASTRO_GEN_DIR}/gen_table.c")
    list(REMOVE_DUPLICATES ASTRO_GEN_SOURCES)
endif()

# Hand-written replacements of core functions (warnings on, unlike the
# generated code).
set(ASTRO_HAND_SOURCES src/core/replaced.c src/core/random.c src/core/sprite.c src/core/pools.c src/core/spawn.c src/core/vectors.c src/core/init.c src/core/keys.c src/core/tables.c src/core/setters.c src/core/shellcall.c src/core/small.c src/core/hitbox.c src/core/counters.c src/core/lists.c src/core/misc.c src/core/recs.c src/core/stage.c src/core/math.c src/core/level.c src/core/texture.c)

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
add_library(astro_core STATIC ${ASTRO_GEN_SOURCES} re/recomp/recomp_rt.c ${ASTRO_HAND_SOURCES} ${ASTRO_GEN_STAMP})
target_include_directories(astro_core PUBLIC re/recomp "${ASTRO_GEN_DIR}" src/core)
if(MSVC)
    set_source_files_properties(${ASTRO_GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "/O1;/w;/bigobj")
else()
    set_source_files_properties(${ASTRO_GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "-O1;-w")
    target_link_libraries(astro_core PUBLIC m)
endif()
if(ASTRO_COVERAGE)
    # Only the hand-written functions: which of their branches the recordings
    # reach (gcov; see docs/recompiler.md).
    set_source_files_properties(${ASTRO_HAND_SOURCES} PROPERTIES COMPILE_OPTIONS "${ASTRO_WARNINGS};--coverage;-O0")
    target_link_options(astro_core PUBLIC --coverage)
else()
    set_source_files_properties(${ASTRO_HAND_SOURCES} PROPERTIES COMPILE_OPTIONS "${ASTRO_WARNINGS}")
endif()
if(ASTRO_DIFF)
    target_compile_definitions(astro_core PUBLIC RT_DIFF)
endif()
if(ASTRO_PROFILE)
    target_compile_definitions(astro_core PUBLIC RT_PROFILE)
endif()

# Frontend-neutral code (the replacement for the original shell), compiled once
# and linked into every frontend.
add_library(astro_common OBJECT
    src/game.c
    src/shell.c
    src/callbacks.c
    src/video.c
    src/input.c
    src/replay.c
    src/assets.c
    src/audio.c
    src/config.c
    src/settings.c
    src/util.c
    src/png.c
    src/scale.c
    src/fuzz.c
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
    enable_testing()
    add_subdirectory(tests)
endif()

if(ASTRO_BOT AND UNIX)
    astro_frontend(astro-bot src/frontend/bot.c)
endif()

# Packaging (tools/make_package.py): on Windows the game is a GUI program (no
# console window; errors go to message boxes), on Linux the executables also
# look for shared libraries in lib/ next to them, where a package bundles SDL.
foreach(t astro astro-sdl2 astro-sdl1)
    if(TARGET ${t})
        if(WIN32 AND t STREQUAL "astro")    # SDL3's SDL_main.h provides WinMain
            set_target_properties(${t} PROPERTIES WIN32_EXECUTABLE ON)
        elseif(UNIX AND NOT APPLE)
            set_target_properties(${t} PROPERTIES BUILD_RPATH "\$ORIGIN/lib")
        endif()
    endif()
endforeach()

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
