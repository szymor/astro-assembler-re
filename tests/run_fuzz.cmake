# The fuzz check of hand-written core functions (src/fuzz.c): astro-headless
# with ASTRO_FUZZ=all must exit cleanly. In the differential build every call
# is compared with the generated version; elsewhere it only runs them.
#
#   cmake -DEXE=<astro-headless> -DASSETS=<dir> -P run_fuzz.cmake
#
# Exits with 77 (ctest: skipped) when the assets are missing.
cmake_minimum_required(VERSION 3.21)

if(NOT EXISTS "${ASSETS}/manifest.json")
    message("assets not found in ${ASSETS} (set ASTRO_TEST_ASSETS); skipping")
    cmake_language(EXIT 77)
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E env --unset=ASTRO_REPLAY --unset=ASTRO_RECORD --unset=ASTRO_KEYS
        ASTRO_FUZZ=all ASTRO_FUZZ_ITER=5000 ASTRO_FUZZ_SEED=1
        "${EXE}" "${ASSETS}"
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
    RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "fuzz check failed (${rc}):\n${out}\n${err}")
endif()
string(REGEX MATCH "fuzz: [0-9]+ functions done" got "${out}")
if(NOT got)
    message(FATAL_ERROR "no fuzz summary in the output:\n${out}")
endif()
message("${got}")
