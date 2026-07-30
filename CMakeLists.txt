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
add_test(NAME decoder_corpus
         COMMAND test_corpus ${CMAKE_SOURCE_DIR}/re/instructions.tsv)

add_executable(lift recompiler/lift.c)
target_link_libraries(lift PRIVATE x86_decode)
target_compile_options(lift PRIVATE -Wall -Wextra)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/gen/lf2_recomp.c
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/gen
    COMMAND lift ${CMAKE_SOURCE_DIR}/game/lf2.exe ${CMAKE_SOURCE_DIR}/re/entries.tsv
                 ${CMAKE_BINARY_DIR}/gen/lf2_recomp.c
    DEPENDS lift ${CMAKE_SOURCE_DIR}/re/entries.tsv
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
    runtime/main.c)
target_include_directories(lf2 PRIVATE runtime)

# Warnings on the hand-written runtime only. The recompiled output is machine-emitted and
# would bury the signal, so it is compiled without them rather than the whole target being
# left unchecked -- which is how four real warnings sat unnoticed.
set_source_files_properties(
    runtime/guest.c runtime/flags.c runtime/strops.c runtime/rwatch.c runtime/imports.c
    runtime/com.c runtime/win32.c runtime/ddraw.c runtime/dsound.c runtime/dshow.c
    runtime/gdi.c runtime/gamepad.c runtime/wsock.c runtime/main.c
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

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/gen/insn_cases.c
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/gen
    COMMAND lift --insn-test ${CMAKE_SOURCE_DIR}/re/instructions.tsv
                 ${CMAKE_BINARY_DIR}/gen/insn_cases.c
    DEPENDS lift ${CMAKE_SOURCE_DIR}/re/instructions.tsv
    COMMENT "Generating instruction differential cases")

add_executable(test_insn ${CMAKE_BINARY_DIR}/gen/insn_cases.c runtime/test_insn.c runtime/flags.c runtime/strops.c runtime/rwatch.c)
target_include_directories(test_insn PRIVATE runtime)
add_test(NAME instructions COMMAND test_insn)
