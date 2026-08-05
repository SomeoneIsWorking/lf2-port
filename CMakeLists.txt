cmake_minimum_required(VERSION 3.16)
project(lf2_port C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(x86_decode recompiler/x86_decode.c)
target_include_directories(x86_decode PUBLIC recompiler)
target_compile_options(x86_decode PRIVATE -Wall -Wextra)

add_executable(test_corpus recompiler/test_corpus.c)
target_link_libraries(test_corpus PRIVATE x86_decode)
target_compile_options(test_corpus PRIVATE -Wall -Wextra)

enable_testing()

# re/instructions.tsv is Ghidra's disassembly of the game: every instruction with its raw
# bytes, which is enough to reconstruct .text. It is a derivative of a binary this project
# does not distribute, so it is gitignored and supplied by whoever runs the build
# (docs/isa-scope.md has the one-line Ghidra command).
#
# It feeds two different tests, and only one of them actually needs Ghidra:
#   decoder_corpus -- checks our decoder against an INDEPENDENT disassembly. Skips (77)
#                     without it, loudly; there is no substitute, by definition.
#   instructions   -- feeds real encodings to the host CPU and to our lift and compares.
#                     A corpus emitted by our own decoder serves that perfectly well, so
#                     one is generated when Ghidra's is absent rather than losing 66,984
#                     checks in a fresh clone.
add_test(NAME decoder_corpus
         COMMAND test_corpus ${CMAKE_SOURCE_DIR}/re/instructions.tsv)
set_tests_properties(decoder_corpus PROPERTIES SKIP_RETURN_CODE 77)

add_executable(lift recompiler/lift.c)
target_link_libraries(lift PRIVATE x86_decode)
target_compile_options(lift PRIVATE -Wall -Wextra)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/gen/lf2_recomp.c
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/gen
    COMMAND lift ${CMAKE_SOURCE_DIR}/game/lf2.exe ${CMAKE_SOURCE_DIR}/re/entries.tsv
                 ${CMAKE_BINARY_DIR}/gen/lf2_recomp.c
    DEPENDS lift ${CMAKE_SOURCE_DIR}/re/entries.tsv ${CMAKE_SOURCE_DIR}/re/overrides.txt
    COMMENT "Recompiling lf2.exe -> C")

add_executable(lf2
    ${CMAKE_BINARY_DIR}/gen/lf2_recomp.c
    runtime/guest.c
    runtime/flags.c
    runtime/strops.c
    runtime/rwatch.c
    runtime/imports.c
    runtime/com.c
    runtime/win32.c
    runtime/ddraw.c
    runtime/dsound.c
    runtime/dshow.c
    runtime/gdi.c
    runtime/loadprof.c
    runtime/pause.c
    runtime/gamepad.c
    runtime/wsock.c
    runtime/overrides/menu.c
    runtime/overrides/screens.c
    runtime/overrides/input.c
    runtime/overrides/coop.c
    runtime/overrides/coop_debug.c
    runtime/overrides/hud.c
    runtime/overrides/text.c
    runtime/overrides/assets.c
    runtime/main.c)
target_include_directories(lf2 PRIVATE runtime runtime/overrides)

# Warnings on the hand-written runtime only. The recompiled output is machine-emitted and
# would bury the signal, so it is compiled without them rather than the whole target being
# left unchecked -- which is how four real warnings sat unnoticed.
set_source_files_properties(
    runtime/guest.c runtime/flags.c runtime/strops.c runtime/rwatch.c runtime/imports.c
    runtime/com.c runtime/win32.c runtime/ddraw.c runtime/dsound.c runtime/dshow.c
    runtime/gdi.c runtime/loadprof.c runtime/pause.c runtime/gamepad.c runtime/wsock.c
    runtime/overrides/menu.c
    runtime/overrides/screens.c
    runtime/overrides/input.c
    runtime/overrides/coop.c
    runtime/overrides/coop_debug.c
    runtime/overrides/hud.c
    runtime/overrides/text.c
    runtime/overrides/assets.c
    runtime/main.c
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
option(LF2_FN_TRACE "Trace guest function entries (debugging)" OFF)
if(LF2_FN_TRACE)
    target_compile_definitions(lf2 PRIVATE LF2_FN_TRACE)
endif()
option(LF2_STACK_CHECK "Assert stack balance at every guest RET (debugging)" OFF)
if(LF2_STACK_CHECK)
    target_compile_definitions(lf2 PRIVATE LF2_STACK_CHECK LF2_FN_TRACE)
endif()

find_package(SDL3 REQUIRED CONFIG)
target_link_libraries(lf2 PRIVATE SDL3::SDL3)

# Optional: a real font for the GDI text path. Without it the built-in debug font is used,
# which is legible but looks like a debug overlay. Kept optional rather than required
# because the port otherwise needs nothing but SDL3.
find_package(SDL3_ttf QUIET CONFIG)
if(SDL3_ttf_FOUND)
    target_link_libraries(lf2 PRIVATE SDL3_ttf::SDL3_ttf)
    target_compile_definitions(lf2 PRIVATE LF2_HAVE_TTF)
    message(STATUS "SDL3_ttf found: GDI text will use a system font")
else()
    message(STATUS "SDL3_ttf not found: GDI text will use SDL's built-in debug font")
endif()

add_executable(test_blit runtime/test_blit.c)
target_include_directories(test_blit PRIVATE runtime)
target_compile_options(test_blit PRIVATE -Wall -Wextra)
add_test(NAME blit COMMAND test_blit)

add_executable(test_mixer runtime/test_mixer.c runtime/mixer.c)
target_include_directories(test_mixer PRIVATE runtime)
target_compile_options(test_mixer PRIVATE -Wall -Wextra)
target_link_libraries(test_mixer PRIVATE m)
add_test(NAME mixer COMMAND test_mixer)

add_executable(test_flags runtime/test_flags.c runtime/flags.c)
target_include_directories(test_flags PRIVATE runtime)
add_test(NAME flags COMMAND test_flags)
set_tests_properties(flags PROPERTIES SKIP_RETURN_CODE 77)

if(EXISTS ${CMAKE_SOURCE_DIR}/re/instructions.tsv)
    set(INSN_TSV ${CMAKE_SOURCE_DIR}/re/instructions.tsv)
    set(INSN_TSV_DEP ${INSN_TSV})
else()
    set(INSN_TSV ${CMAKE_BINARY_DIR}/gen/instructions.tsv)
    set(INSN_TSV_DEP ${INSN_TSV})
    add_custom_command(
        OUTPUT ${INSN_TSV}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/gen
        COMMAND lift --dump-insns ${CMAKE_SOURCE_DIR}/game/lf2.exe
                     ${CMAKE_SOURCE_DIR}/re/entries.tsv ${INSN_TSV}
        DEPENDS lift ${CMAKE_SOURCE_DIR}/re/entries.tsv
        COMMENT "No Ghidra dump present; deriving an instruction corpus from the binary")
endif()

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/gen/insn_cases.c
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/gen
    COMMAND lift --insn-test ${INSN_TSV} ${CMAKE_BINARY_DIR}/gen/insn_cases.c
    DEPENDS lift ${INSN_TSV_DEP}
    COMMENT "Generating instruction differential cases")

add_executable(test_insn ${CMAKE_BINARY_DIR}/gen/insn_cases.c runtime/test_insn.c runtime/flags.c runtime/strops.c runtime/rwatch.c)
target_include_directories(test_insn PRIVATE runtime)
# The committed golden vectors let a non-x86 host replay the x86 truth; on such a host
# the bare `test_insn` run below automatically becomes a replay against this file.
target_compile_definitions(test_insn PRIVATE
    LF2_VECTORS_PATH="${CMAKE_SOURCE_DIR}/re/insn_vectors.bin")
add_test(NAME instructions COMMAND test_insn)
set_tests_properties(instructions PROPERTIES SKIP_RETURN_CODE 77)

# Regenerate the vectors (x86 only): cmake --build . --target insn_vectors
add_custom_target(insn_vectors
    COMMAND test_insn --capture ${CMAKE_SOURCE_DIR}/re/insn_vectors.bin
    DEPENDS test_insn
    COMMENT "Capturing instruction golden vectors from the host CPU")

# On x86 the replay path itself is exercised against the committed vectors, so a broken
# replay cannot hide behind the live differential until someone runs it on a Mac.
if(EXISTS ${CMAKE_SOURCE_DIR}/re/insn_vectors.bin)
    add_test(NAME instructions_replay
             COMMAND test_insn --replay ${CMAKE_SOURCE_DIR}/re/insn_vectors.bin)
    set_tests_properties(instructions_replay PROPERTIES SKIP_RETURN_CODE 77)
endif()

# End-to-end: drives the port to a running match and asserts the things that have actually
# broken before. Slow (~75 s) and needs the extracted game tree, so it is labelled and can
# be skipped with `ctest -LE slow`. It skips itself (exit 77) if the tree is absent.
#
# Every `slow` test is RUN_SERIAL. Each one launches a full game instance and wraps it in a
# wall-clock `timeout`, and two instances on one machine take long enough to trip it -- so
# `ctest -j2` produced FAILURES from tests that pass individually and pass serially. A
# flaky gate is worse than a slow one: it teaches you to re-run until green, which is how a
# real regression gets waved through. They are serialised rather than given longer
# timeouts, because the timeout is what stops a hung run from wedging the suite.
add_test(NAME smoke
         COMMAND sh ${CMAKE_SOURCE_DIR}/tools/smoke_test.sh
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(smoke PROPERTIES
                     LABELS slow TIMEOUT 180 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE
                     ENVIRONMENT "BUILD=${CMAKE_BINARY_DIR}")

# The controller path, which the mouse-driven smoke test cannot see at all. Kept separate
# rather than folded in: it supplies no keyboard and no mouse input, so a failure names the
# controller support rather than being one more assertion in a run that does everything.
add_test(NAME controller
         COMMAND sh ${CMAKE_SOURCE_DIR}/tools/controller_test.sh
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(controller PROPERTIES
                     LABELS slow TIMEOUT 180 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE
                     ENVIRONMENT "BUILD=${CMAKE_BINARY_DIR}")

# The mouse path, for the same reason the controller has its own test: the smoke test
# clicks once and then drives everything else from the keyboard, so every screen after the
# launcher was proved only for keys. Mouse support on those screens could have been dead
# and stayed green -- and part of it was.
add_test(NAME mouse
         COMMAND sh ${CMAKE_SOURCE_DIR}/tools/mouse_test.sh
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(mouse PROPERTIES
                     LABELS slow TIMEOUT 180 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE
                     ENVIRONMENT "BUILD=${CMAKE_BINARY_DIR}")

# Two controllers, two players. Separate because it needs two runs -- one as a control --
# and because "a second pad is player two" has been got wrong in both directions already.
add_test(NAME controller_2p
         COMMAND sh ${CMAKE_SOURCE_DIR}/tools/controller_2p_test.sh
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(controller_2p PROPERTIES
                     LABELS slow TIMEOUT 300 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE
                     ENVIRONMENT "BUILD=${CMAKE_BINARY_DIR}")

# Drop-in coop: a second pad joins a match that is already running and drives its fighter.
# Two-sided -- the same join with and without a direction pressed -- because "a fighter
# appeared" would also be satisfied by one wandering under its own AI.
add_test(NAME coop_dropin
         COMMAND sh ${CMAKE_SOURCE_DIR}/tools/coop_dropin_test.sh
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(coop_dropin PROPERTIES
                     LABELS slow TIMEOUT 300 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE
                     ENVIRONMENT "BUILD=${CMAKE_BINARY_DIR}")

# A late joiner CHOOSES: the fighter flashes, left/right cycle the game's own roster and
# attack locks in. Separate from coop_dropin because the two measure different halves --
# coop_dropin that the pad drives the fighter it joined, this one that the player picked it.
add_test(NAME coop_select
         COMMAND sh ${CMAKE_SOURCE_DIR}/tools/coop_select_test.sh
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(coop_select PROPERTIES
                     LABELS slow TIMEOUT 300 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE
                     ENVIRONMENT "BUILD=${CMAKE_BINARY_DIR}")

# A joined player leaves a match deliberately, from the pause menu on its own pad. The
# drop-out machinery was previously reached only by unplugging a pad, which no test can do.
add_test(NAME pause_dropout
         COMMAND sh ${CMAKE_SOURCE_DIR}/tools/pause_dropout_test.sh
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(pause_dropout PROPERTIES
                     LABELS slow TIMEOUT 320 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE
                     ENVIRONMENT "BUILD=${CMAKE_BINARY_DIR}")

# The composition's width follows the WINDOW's aspect, in both directions and with the
# native and taller-than-native cases as the negatives (issue #20).
add_test(NAME widescreen
         COMMAND sh ${CMAKE_SOURCE_DIR}/tools/widescreen_test.sh
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(widescreen PROPERTIES
                     LABELS slow TIMEOUT 300 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE
                     ENVIRONMENT "BUILD=${CMAKE_BINARY_DIR}")

# Two humans in a MATCH. controller_2p proves the second pad joins at character selection
# and stops there; this one reaches the fight and asserts pad two actually drives its
# fighter, two-sided.
add_test(NAME two_human_match
         COMMAND sh ${CMAKE_SOURCE_DIR}/tools/two_human_match_test.sh
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(two_human_match PROPERTIES
                     LABELS slow TIMEOUT 300 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE
                     ENVIRONMENT "BUILD=${CMAKE_BINARY_DIR}")
