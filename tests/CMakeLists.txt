# Regression tests: astro-headless runs whose final frame and audio hashes must
# not change. Recordings (replays/*.rec, see src/replay.c) hold only inputs, no
# game data; the tests need the packaged assets (tools/package_assets.py) and
# are skipped without them. The expected hashes (expected/*.txt) are the port's
# own earlier output, not the original game's: a changed hash means the output
# changed and must be explained. ASTRO_BLESS=1 ctest rewrites them.
#
#   ctest --test-dir build/cmake --output-on-failure
set(ASTRO_TEST_ASSETS "${CMAKE_SOURCE_DIR}/assets" CACHE PATH "Packaged assets for the regression tests")

set(_dir "${CMAKE_CURRENT_SOURCE_DIR}")

# astro_regression(<name> [REPLAY <file>] [ENV VAR=value...])
function(astro_regression name)
    cmake_parse_arguments(A "" "REPLAY" "ENV" ${ARGN})
    set(args -DEXE=$<TARGET_FILE:astro-headless> -DASSETS=${ASTRO_TEST_ASSETS}
             -DEXPECTED=${_dir}/expected/${name}.txt)
    if(A_REPLAY)
        list(APPEND args -DREPLAY=${_dir}/replays/${A_REPLAY})
    endif()
    if(A_ENV)
        string(REPLACE ";" "\\;" env "${A_ENV}")
        list(APPEND args "-DENV=${env}")
    endif()
    add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} ${args} -P ${_dir}/run_headless.cmake)
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 LABELS regression)
endfunction()

# The scripted-input path (ASTRO_KEYS) and the same session as a recording.
astro_regression(keys-level1 ENV ASTRO_FRAMES=3000 "ASTRO_KEYS=800:39,1100:1c,1700:39:600,1700:19:120")
astro_regression(replay-level1 REPLAY level1-keys.rec)
astro_regression(replay-level1-320x240 REPLAY level1-keys.rec ENV ASTRO_RESOLUTION=320x240)
astro_regression(replay-level1-320x240-16x10 REPLAY level1-keys.rec ENV ASTRO_RESOLUTION=320x240 ASTRO_GAMEPLAY_ASPECT=16:10)

# Recorded with the SDL3 frontend and a (virtual) gamepad: joystick polls.
astro_regression(replay-level1-gamepad REPLAY level1-gamepad-sdl3.rec)

# Game over with a hall of fame entry (crafted starting table), and a
# two-player game set up through the options menu.
astro_regression(replay-hall-of-fame REPLAY hall-of-fame.rec)
astro_regression(replay-two-players REPLAY two-players.rec)

# Each later level started directly (ASTRO_LEVEL in the recording), and the ending.
foreach(n 2 3 4 5 6)
    astro_regression(replay-level${n}-start REPLAY level${n}-start.rec)
endforeach()
astro_regression(replay-ending REPLAY ending.rec)

# Every *.rec dropped into replays/full/ (e.g. whole playthroughs recorded with
# ASTRO_RECORD) becomes a test of its own; bless it once to record its hashes.
file(GLOB _full RELATIVE "${_dir}/replays" CONFIGURE_DEPENDS "${_dir}/replays/full/*.rec")
foreach(r IN LISTS _full)
    get_filename_component(n "${r}" NAME_WE)
    astro_regression(replay-full-${n} REPLAY ${r})
endforeach()
