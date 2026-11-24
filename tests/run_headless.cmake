# One regression test: run astro-headless and compare its final hash line with
# the expected one.
#
#   cmake -DEXE=<astro-headless> -DASSETS=<dir> -DEXPECTED=<file>
#         [-DREPLAY=<file>] [-DENV=VAR=value;VAR=value...] -P run_headless.cmake
#
# With the environment variable ASTRO_BLESS=1 the expected file is (re)written
# from the run instead. Exits with 77 (ctest: skipped) when the assets are missing.
cmake_minimum_required(VERSION 3.21)

if(NOT EXISTS "${ASSETS}/manifest.json")
    message("assets not found in ${ASSETS} (set ASTRO_TEST_ASSETS); skipping")
    cmake_language(EXIT 77)
endif()

set(env_args)
if(REPLAY)
    list(APPEND env_args "ASTRO_REPLAY=${REPLAY}")
endif()
foreach(e IN LISTS ENV)
    list(APPEND env_args "${e}")
endforeach()

# Start from a clean environment as far as the game is concerned.
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env
        --unset=ASTRO_KEYS --unset=ASTRO_FRAMES --unset=ASTRO_LEVEL --unset=ASTRO_ENDING
        --unset=ASTRO_RECORD --unset=ASTRO_REPLAY --unset=ASTRO_DUMP --unset=ASTRO_HASH_EVERY
        --unset=ASTRO_RESOLUTION --unset=ASTRO_GAMEPLAY_ASPECT
        ${env_args}
        "${EXE}" "${ASSETS}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)

if(NOT rc EQUAL 0)
    message(FATAL_ERROR "astro-headless failed (${rc}):\n${err}")
endif()
string(REGEX MATCH "headless frames=[0-9]+ video=[0-9a-f]+ audio=[0-9a-f]+" got "${out}")
if(NOT got)
    message(FATAL_ERROR "no hash line in the output:\n${out}\n${err}")
endif()
if(err MATCHES "replay out of step")
    message(FATAL_ERROR "${err}")
endif()

if("$ENV{ASTRO_BLESS}" STREQUAL "1")
    file(WRITE "${EXPECTED}" "${got}\n")
    message("blessed: ${got}")
    return()
endif()

if(NOT EXISTS "${EXPECTED}")
    message(FATAL_ERROR "no expected result ${EXPECTED}; got\n  ${got}\n(run with ASTRO_BLESS=1 to accept)")
endif()
file(STRINGS "${EXPECTED}" want LIMIT_COUNT 1)
if(NOT got STREQUAL want)
    message(FATAL_ERROR "output changed\n  expected: ${want}\n  got:      ${got}")
endif()
message("${got}")
