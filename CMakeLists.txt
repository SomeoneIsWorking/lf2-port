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
# tests/test_geom.c rather than in a script. The audio pan is the worked example: a
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
    runtime/cpu/guest.c
    runtime/cpu/flags.c
    runtime/cpu/strops.c
    runtime/cpu/rwatch.c
    runtime/win32/imports.c
    runtime/win32/com.c
    runtime/win32/win32.c
    runtime/video/ddraw.c
    runtime/audio/dsound.c
    runtime/win32/dshow.c
    runtime/win32/gdi.c
    runtime/app/loadprof.c
    runtime/app/pause.c
    runtime/input/gamepad.c
    runtime/app/script.c
    runtime/win32/wsock.c
    runtime/overrides/menu.c
    runtime/overrides/screens.c
    runtime/overrides/input.c
    runtime/overrides/coop.c
    runtime/overrides/coop_debug.c
    runtime/overrides/hud.c
    runtime/overrides/text.c
    runtime/overrides/assets.c
    runtime/overrides/background.c
    runtime/overrides/objects.c
    runtime/overrides/audio_pan.c
    runtime/video/render.c
    runtime/video/hd2d.c
    runtime/video/mesh.c
    runtime/app/main.c)
# EVERY subsystem directory is on the include path, which is what lets a file say
# #include "guest_ops.h" wherever it happens to live. The alternative -- relative "../"
# includes -- ties each file to its position in the tree and turns any future regrouping into
# an edit of every header line (issue #46).
target_include_directories(lf2 PRIVATE
    runtime runtime/cpu runtime/win32 runtime/video runtime/audio runtime/input runtime/app
    runtime/overrides)

# Warnings on the hand-written runtime only. The recompiled output is machine-emitted and
# would bury the signal, so it is compiled without them rather than the whole target being
# left unchecked -- which is how four real warnings sat unnoticed.
set_source_files_properties(
    runtime/cpu/guest.c runtime/cpu/flags.c runtime/cpu/strops.c runtime/cpu/rwatch.c runtime/win32/imports.c
    runtime/win32/com.c runtime/win32/win32.c runtime/video/ddraw.c runtime/audio/dsound.c runtime/win32/dshow.c
    runtime/win32/gdi.c runtime/app/loadprof.c runtime/app/pause.c runtime/input/gamepad.c runtime/app/script.c
    runtime/win32/wsock.c
    runtime/overrides/menu.c
    runtime/overrides/screens.c
    runtime/overrides/input.c
    runtime/overrides/coop.c
    runtime/overrides/coop_debug.c
    runtime/overrides/hud.c
    runtime/overrides/text.c
    runtime/overrides/assets.c
    runtime/overrides/background.c
    runtime/overrides/objects.c
    runtime/overrides/audio_pan.c
    runtime/video/render.c
    runtime/video/hd2d.c
    runtime/video/mesh.c
    runtime/app/main.c
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
# libm: the light rig turns the player's two angles into a direction vector (runtime/video/hd2d.c).
# gcc folds most of <math.h> inline and then does not, so this is linked explicitly rather
# than relied on to keep being unnecessary.
find_library(LF2_M m)
target_link_libraries(lf2 PRIVATE SDL3::SDL3 $<$<BOOL:${LF2_M}>:${LF2_M}>)

# ---- the text faces, COMPILED IN (issue #45) ----
#
# assets/fonts/ holds two unmodified SIL OFL 1.1 faces, and they are turned into byte arrays
# here rather than opened from disk at runtime. That is the point: there is no font search, no
# system font, and no fallback. A port whose text silently changes with whatever the host has
# installed is a port whose screenshots cannot be compared with each other -- and the previous
# arrangement did exactly that, walking a list of nine candidate paths and quietly dropping to
# SDL's 8x8 debug font when none matched.
#
# file(READ ... HEX) at configure time, so the build needs no extra tool -- not python, not
# xxd. 730 KB of font becomes about 4.4 MB of C, which is why the newlines matter: one
# 4-million-character line is legal C that some compilers take a very long time over.
function(lf2_embed_font symbol src)
    set(out "${CMAKE_BINARY_DIR}/gen/${symbol}.c")
    if(NOT EXISTS "${src}")
        message(FATAL_ERROR "font ${src} is missing -- it is committed to this repo and the "
                            "binary cannot be built without it")
    endif()
    file(READ "${src}" hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," body "${hex}")
    string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){16})" "\\1\n" body "${body}")
    file(WRITE "${out}"
         "/* Generated from ${src} by CMakeLists.txt -- do not edit, do not commit. */\n"
         "const unsigned char ${symbol}[] = {\n${body}\n};\n"
         "const unsigned int ${symbol}_len = (unsigned int)(sizeof ${symbol});\n")
    set(LF2_FONT_SOURCES ${LF2_FONT_SOURCES} "${out}" PARENT_SCOPE)
endfunction()

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/gen)
set(LF2_FONT_SOURCES "")
lf2_embed_font(lf2_font_sans ${CMAKE_SOURCE_DIR}/assets/fonts/LiberationSans-Regular.ttf)
lf2_embed_font(lf2_font_mono ${CMAKE_SOURCE_DIR}/assets/fonts/LiberationMono-Regular.ttf)
target_sources(lf2 PRIVATE ${LF2_FONT_SOURCES})

# SDL3_ttf rasterises them. It is REQUIRED, and that is a change: it used to be optional with
# a debug-font fallback, which meant a build could silently produce a different game. A missing
# dependency must stop the build, not degrade the product.
find_package(SDL3_ttf QUIET CONFIG)
if(SDL3_ttf_FOUND)
    target_link_libraries(lf2 PRIVATE SDL3_ttf::SDL3_ttf)
    target_compile_definitions(lf2 PRIVATE LF2_HAVE_TTF)
    message(STATUS "SDL3_ttf found: text is drawn from the committed Liberation faces")
else()
    message(FATAL_ERROR
            "SDL3_ttf not found. It is required: the port draws all of its text from the "
            "outline fonts committed in assets/fonts/, and the debug-font fallback that used "
            "to cover this case was removed because it silently changed what the player saw "
            "(issue #45). Install SDL3_ttf (Fedora: sdl3-ttf-devel).")
endif()

# The port's geometry, tested WITHOUT booting the game. Every claim in it used to cost a
# three-thousand-frame headless run; they are arithmetic, and runtime/overrides/geom.h is that
# arithmetic factored out so the overrides and this test share one copy of it.
add_executable(test_geom tests/test_geom.c)
target_include_directories(test_geom PRIVATE runtime runtime/overrides runtime/cpu)
target_compile_options(test_geom PRIVATE -Wall -Wextra)
add_test(NAME geometry COMMAND test_geom)

# The renderer's frame lifetime -- when a display list is cleared, how a frame held up under
# the pause menu is rewound, which pooled texture serves a tile. runtime/video/render.c
# includes the same header, so this is not a copy. No SDL, no window, no game.
add_executable(test_framelife tests/test_framelife.c)
target_include_directories(test_framelife PRIVATE runtime runtime/video)
target_compile_options(test_framelife PRIVATE -Wall -Wextra)
add_test(NAME framelife COMMAND test_framelife)

add_executable(test_framespec tests/test_framespec.c)
target_include_directories(test_framespec PRIVATE runtime runtime/app)
target_compile_options(test_framespec PRIVATE -Wall -Wextra)
add_test(NAME framespec COMMAND test_framespec)

add_executable(test_blit tests/test_blit.c)
target_include_directories(test_blit PRIVATE runtime runtime/cpu runtime/audio runtime/video runtime/win32)
target_compile_options(test_blit PRIVATE -Wall -Wextra)
add_test(NAME blit COMMAND test_blit)

add_executable(test_mixer tests/test_mixer.c runtime/audio/mixer.c)
target_include_directories(test_mixer PRIVATE runtime runtime/cpu runtime/audio runtime/video runtime/win32)
target_compile_options(test_mixer PRIVATE -Wall -Wextra)
target_link_libraries(test_mixer PRIVATE m)
add_test(NAME mixer COMMAND test_mixer)

add_executable(test_flags tests/test_flags.c runtime/cpu/flags.c)
target_include_directories(test_flags PRIVATE runtime runtime/cpu runtime/audio runtime/video runtime/win32)
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

add_executable(test_insn ${CMAKE_BINARY_DIR}/gen/insn_cases.c tests/test_insn.c runtime/cpu/flags.c runtime/cpu/strops.c runtime/cpu/rwatch.c)
target_include_directories(test_insn PRIVATE runtime runtime/cpu runtime/audio runtime/video runtime/win32)
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
         COMMAND sh ${CMAKE_SOURCE_DIR}/tools/build/build_shaders.sh --check
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(shaders PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)