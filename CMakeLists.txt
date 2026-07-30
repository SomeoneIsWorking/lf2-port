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
    runtime/gamepad.c
    runtime/wsock.c
    runtime/overrides.c
    runtime/main.c)
target_include_directories(lf2 PRIVATE runtime)

# Warnings on the hand-written runtime only. The recompiled output is machine-emitted and
# would bury the signal, so it is compiled without them rather than the whole target being
# left unchecked -- which is how four real warnings sat unnoticed.
set_source_files_properties(
    runtime/guest.c runtime/flags.c runtime/strops.c runtime/rwatch.c runtime/imports.c
    runtime/com.c runtime/win32.c runtime/ddraw.c runtime/dsound.c runtime/dshow.c
    runtime/gdi.c runtime/gamepad.c runtime/wsock.c runtime/overrides.c runtime/main.c
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

add_executable(test_flags runtime/test_flags.c runtime/flags.c)
target_include_directories(test_flags PRIVATE runtime)
add_test(NAME flags COMMAND test_flags)

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
add_test(NAME instructions COMMAND test_insn)
set_tests_properties(instructions PROPERTIES SKIP_RETURN_CODE 77)

# End-to-end: drives the port to a running match and asserts the things that have actually
# broken before. Slow (~75 s) and needs the extracted game tree, so it is labelled and can
# be skipped with `ctest -LE slow`. It skips itself (exit 77) if the tree is absent.
add_test(NAME smoke
         COMMAND sh ${CMAKE_SOURCE_DIR}/tools/smoke_test.sh
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(smoke PROPERTIES
                     LABELS slow TIMEOUT 180 SKIP_RETURN_CODE 77
                     ENVIRONMENT "BUILD=${CMAKE_BINARY_DIR}")

# The controller path, which the mouse-driven smoke test cannot see at all. Kept separate
# rather than folded in: it supplies no keyboard and no mouse input, so a failure names the
# controller support rather than being one more assertion in a run that does everything.
add_test(NAME controller
         COMMAND sh ${CMAKE_SOURCE_DIR}/tools/controller_test.sh
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(controller PROPERTIES
                     LABELS slow TIMEOUT 180 SKIP_RETURN_CODE 77
                     ENVIRONMENT "BUILD=${CMAKE_BINARY_DIR}")
