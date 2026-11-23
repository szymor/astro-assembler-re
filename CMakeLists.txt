cmake_minimum_required(VERSION 3.20)
project(astro_assembler C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Generated C of the recompiled assembly core (re/recomp/recomp.py). It is
# derived from ASTRO.exe and therefore built locally, never committed.
set(ASTRO_GEN_DIR "${CMAKE_SOURCE_DIR}/build/recomp/gen" CACHE PATH "Directory with the generated core (gen_*.c)")

file(GLOB ASTRO_GEN_SOURCES "${ASTRO_GEN_DIR}/gen_*.c")
if(NOT ASTRO_GEN_SOURCES)
    message(FATAL_ERROR "No generated core found in ${ASTRO_GEN_DIR}. See docs/recompiler.md.")
endif()

find_package(SDL3 REQUIRED CONFIG)
find_package(Threads REQUIRED)         # some FLAC CMake configs reference Threads::Threads
find_package(FLAC REQUIRED CONFIG)     # libFLAC (vcpkg: "flac") for the soundtrack

# The generated code is huge and mechanical: build it as its own library with
# moderate optimisation and without warnings.
add_library(astro_core STATIC ${ASTRO_GEN_SOURCES} re/recomp/recomp_rt.c)
target_include_directories(astro_core PUBLIC re/recomp "${ASTRO_GEN_DIR}")
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(astro_core PRIVATE -O1 -w)
endif()
if(NOT MSVC)
    target_link_libraries(astro_core PUBLIC m)
endif()

add_executable(astro
    src/main.c
    src/shell.c
    src/callbacks.c
    src/video.c
    src/input.c
    src/assets.c
    src/audio.c
)
target_link_libraries(astro PRIVATE astro_core SDL3::SDL3 FLAC::FLAC)
if(NOT MSVC)
    target_link_libraries(astro PRIVATE m)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(astro PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
