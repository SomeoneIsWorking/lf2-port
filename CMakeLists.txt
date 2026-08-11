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

# ---- THERE IS ONE SUITE AND IT IS FAST ----
#
# `ctest` finishes in about a second and a half, and no single test in it takes half a second.
# That is the whole bar: this is a 2001 game whose entire logic is arithmetic over a few
# megabytes, so anything here that took minutes was measuring the wrong thing in the wrong
# place. Run it after every edit; it is what is allowed to be the reason you believe something.
#
# The game-booting scripts under tools/ are NOT registered here and must not be. They drive a
# real instance through thousands of frames of menus, which answers questions nothing offline
# can -- does a route REACH a screen, does a second pad drive its fighter, does the GPU
# renderer draw what the software one draws -- and costs seconds to minutes each. They live
# behind one command:
#
#     tools/e2e.sh              # all of them, serially
#     tools/e2e.sh mouse render # by name
#
# A claim that COULD be checked offline belongs in runtime/overrides/geom.h and
# runtime/test_geom.c rather than in a script. The audio pan is the worked example: a
# three-run, 270-second script became 20 assertions in a millisecond, and gained a walk across
# every on-screen pixel that the script had never done.

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
    runtime/script.c
    runtime/wsock.c
    runtime/overrides/menu.c
    runtime/overrides/screens.c
    runtime/overrides/input.c
    runtime/overrides/coop.c
    runtime/overrides/coop_debug.c
    runtime/overrides/hud.c
    runtime/overrides/text.c
    runtime/overrides/assets.c
    runtime/overrides/background.c
    runtime/overrides/audio_pan.c
    runtime/render.c
    runtime/hd2d.c
    runtime/main.c)
target_include_directories(lf2 PRIVATE runtime runtime/overrides)

# Warnings on the hand-written runtime only. The recompiled output is machine-emitted and
# would bury the signal, so it is compiled without them rather than the whole target being
# left unchecked -- which is how four real warnings sat unnoticed.
set_source_files_properties(
    runtime/guest.c runtime/flags.c runtime/strops.c runtime/rwatch.c runtime/imports.c
    runtime/com.c runtime/win32.c runtime/ddraw.c runtime/dsound.c runtime/dshow.c
    runtime/gdi.c runtime/loadprof.c runtime/pause.c runtime/gamepad.c runtime/script.c
    runtime/wsock.c
    runtime/overrides/menu.c
    runtime/overrides/screens.c
    runtime/overrides/input.c
    runtime/overrides/coop.c
    runtime/overrides/coop_debug.c
    runtime/overrides/hud.c
    runtime/overrides/text.c
    runtime/overrides/assets.c
    runtime/overrides/background.c
    runtime/overrides/audio_pan.c
    runtime/render.c
    runtime/hd2d.c
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
# libm: the light rig turns the player's two angles into a direction vector (runtime/hd2d.c).
# gcc folds most of <math.h> inline and then does not, so this is linked explicitly rather
# than relied on to keep being unnecessary.
find_library(LF2_M m)
target_link_libraries(lf2 PRIVATE SDL3::SDL3 $<$<BOOL:${LF2_M}>:${LF2_M}>)

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

# The port's geometry, tested WITHOUT booting the game. Every claim in it used to cost a
# three-thousand-frame headless run; they are arithmetic, and runtime/overrides/geom.h is that
# arithmetic factored out so the overrides and this test share one copy of it.
add_executable(test_geom runtime/test_geom.c)
target_include_directories(test_geom PRIVATE runtime runtime/overrides)
target_compile_options(test_geom PRIVATE -Wall -Wextra)
add_test(NAME geometry COMMAND test_geom)

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

# The SPIR-V in runtime/shaders/gen/ is committed, so the build needs no shader compiler --
# which also means an edited shader that was never recompiled would ship the old lighting and
# nothing would say so. This recompiles and compares wherever glslc exists, and SKIPS (77)
# where it does not, saying which.
add_test(NAME shaders
         COMMAND sh ${CMAKE_SOURCE_DIR}/tools/build_shaders.sh --check
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(shaders PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)