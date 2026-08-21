cmake_minimum_required(VERSION 3.16)
project(lf2_port C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR
            "LF2 requires Clang for both C and C++. Configure a clean tree with "
            "tools/build/build.py; GCC and G++ are not supported build evidence.")
endif()

add_library(x86_decode recompiler/x86_decode.c)
target_include_directories(x86_decode PUBLIC recompiler)
target_compile_options(x86_decode PRIVATE -Wall -Wextra)

add_executable(test_corpus recompiler/test_corpus.c)
target_link_libraries(test_corpus PRIVATE x86_decode)
target_compile_options(test_corpus PRIVATE -Wall -Wextra)

enable_testing()

# ---- THERE IS ONE SUITE AND IT IS FAST ----
#
# `ctest` finishes in about fifteen seconds, including formatting and clang-tidy, and nothing in it boots the game.
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
#     tools/e2e.py              # all of them, serially
#     tools/e2e.py mouse render # by name
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
    runtime/video/blt_trace.c
    runtime/video/ddraw.c
    runtime/video/result_panel.c
    runtime/audio/dsound.c
    runtime/win32/dshow.c
    runtime/win32/gdi.c
    runtime/app/loadprof.c
    runtime/app/pause.c
    runtime/app/options.c
    runtime/app/config.c
    runtime/app/startup.c
    runtime/input/gamepad.c
    runtime/input/bindings.c
    runtime/input/keyboard.c
    runtime/app/script.c
    runtime/win32/wsock.c
    runtime/overrides/boot_guest.c
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
    runtime/video/engine.c
    runtime/video/engine_textures.c
    runtime/video/stagegeom.c
    runtime/ui/device_assets.c
    runtime/ui/device_icons.c
    runtime/ui/settings_ui.cpp
    runtime/ui/rmlui_input.cpp
    runtime/ui/rmlui_backend.cpp
    third_party/RmlUi/Backends/RmlUi_Platform_SDL.cpp
    runtime/app/main.c)
# EVERY subsystem directory is on the include path, which is what lets a file say
# #include "guest_ops.h" wherever it happens to live. The alternative -- relative "../"
# includes -- ties each file to its position in the tree and turns any future regrouping into
# an edit of every header line (issue #46).
target_include_directories(lf2 PRIVATE
    runtime runtime/cpu runtime/win32 runtime/video runtime/audio runtime/input runtime/app runtime/ui
    runtime/overrides
    ${CMAKE_SOURCE_DIR}/third_party/RmlUi/Backends)
# RmlUi's SDL backends pick SDL2 vs SDL3 by this macro (Backends/RmlUi_Platform_SDL.h).
target_compile_definitions(lf2 PRIVATE RMLUI_SDL_VERSION_MAJOR=3)

# The hand-woven stage geometry (issue #62) travels BESIDE THE BINARY.
#
# It cannot travel in the working directory: the port runs with its cwd in the GAME TREE,
# because the game opens every one of its own files by relative path, and the game tree is
# neither in this repo nor shipped by it. It cannot be an absolute path baked in at configure
# time either -- that is exactly the machine-specific tracked path this project does not
# commit. So the repo's `stages/` is copied next to the executable after every build, which is
# also where an installed port would put it, and the runtime looks there first (see
# `stage_geom_sync` in runtime/overrides/background.c).
#
# A custom TARGET rather than a POST_BUILD command, and the difference is not cosmetic:
# POST_BUILD only fires when the target relinks, so an author who adds a .stage and rebuilds
# gets nothing copied and a game that reports no geometry -- which is exactly the silent
# failure this whole subsystem is careful about. A custom target runs every build.
#
# Replace the destination before copying. `copy_directory_if_different` does not remove files
# deleted at the source, which let the rejected prop pack keep loading after every source OBJ and
# scene had been deleted. The destination is generated build output, so it must mirror rather
# than accumulate.
add_custom_target(stages ALL
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_BINARY_DIR}/stages
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/stages ${CMAKE_CURRENT_BINARY_DIR}/stages
    COMMENT "stages/ -> beside the binary")
add_dependencies(lf2 stages)

# Warnings on the hand-written runtime only. The recompiled output is machine-emitted and
# would bury the signal, so it is compiled without them rather than the whole target being
# left unchecked -- which is how four real warnings sat unnoticed.
set_source_files_properties(
    runtime/cpu/guest.c runtime/cpu/flags.c runtime/cpu/strops.c runtime/cpu/rwatch.c runtime/win32/imports.c
    runtime/win32/com.c runtime/win32/win32.c runtime/video/blt_trace.c runtime/video/ddraw.c runtime/video/result_panel.c runtime/audio/dsound.c runtime/win32/dshow.c
    runtime/win32/gdi.c runtime/app/loadprof.c runtime/app/pause.c runtime/input/gamepad.c runtime/input/bindings.c runtime/input/keyboard.c runtime/app/script.c
    runtime/app/options.c
    runtime/app/config.c
    runtime/app/startup.c
    runtime/win32/wsock.c
    runtime/overrides/boot_guest.c
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
    runtime/video/engine.c
    runtime/video/engine_textures.c
    runtime/video/stagegeom.c
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

add_executable(test_bindings tests/test_bindings.c runtime/input/bindings.c runtime/app/config.c)
target_include_directories(test_bindings PRIVATE runtime/input runtime/app)
target_link_libraries(test_bindings PRIVATE SDL3::SDL3)
target_compile_options(test_bindings PRIVATE -Wall -Wextra)
set(LF2_BINDINGS_TEST_CONFIG "${CMAKE_BINARY_DIR}/test-bindings.cfg")
add_test(NAME bindings_write COMMAND test_bindings write ${LF2_BINDINGS_TEST_CONFIG})
add_test(NAME bindings_read COMMAND test_bindings read ${LF2_BINDINGS_TEST_CONFIG})
set_tests_properties(bindings_read PROPERTIES DEPENDS bindings_write)

add_executable(test_keyboard tests/test_keyboard.c runtime/input/keyboard.c)
target_include_directories(test_keyboard PRIVATE runtime/input)
target_link_libraries(test_keyboard PRIVATE SDL3::SDL3)
target_compile_options(test_keyboard PRIVATE -Wall -Wextra)
add_test(NAME keyboard COMMAND test_keyboard)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME structure COMMAND ${Python3_EXECUTABLE}
         ${CMAKE_SOURCE_DIR}/tools/build/check_structure.py)
add_test(NAME clang_format COMMAND ${Python3_EXECUTABLE}
         ${CMAKE_SOURCE_DIR}/tools/build/check_cpp_quality.py format)
add_test(NAME clang_tidy COMMAND ${Python3_EXECUTABLE}
         ${CMAKE_SOURCE_DIR}/tools/build/check_cpp_quality.py tidy
         --build-dir ${CMAKE_BINARY_DIR})

# ---- RmlUi (issue #70) ----
#
# The settings UI -- device mapping and the graphical tweaks -- is built with RmlUi, a C++
# library, pinned as a git submodule at third_party/RmlUi. It is the one C++ dependency in a
# port whose build was "a C compiler and SDL": the settings screen is a real document tree
# (RML/CSS + data bindings), which is exactly the case the hand-rolled pause-menu pages were
# never going to scale to. The submodule must be initialised (git submodule update --init),
# and FreeType is required for RmlUi's default font engine. Everything else is OFF on purpose:
# no samples, no tests, no Lua/Lottie, no shared lib. RmlUi's SVG plugin stays off because the
# shared device SVGs are embedded and rasterised through the port's SDL3_image asset boundary.
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/RmlUi/CMakeLists.txt")
    set(BUILD_SHARED_LIBS OFF)
    set(RMLUI_SAMPLES OFF)
    set(RMLUI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF)
    set(RMLUI_LUA_BINDINGS OFF)
    set(RMLUI_LOTTIE_PLUGIN OFF)
    set(RMLUI_SVG_PLUGIN OFF)
    set(RMLUI_BACKEND_AUTO OFF)
    set(RMLUI_FONT_ENGINE "freetype")
    add_subdirectory(third_party/RmlUi)
    target_link_libraries(lf2 PRIVATE RmlUi::Core)
else()
    message(FATAL_ERROR "third_party/RmlUi is missing -- run `git submodule update --init --recursive`")
endif()

find_package(SDL3_image QUIET CONFIG)
if(SDL3_image_FOUND)
    target_link_libraries(lf2 PRIVATE SDL3_image::SDL3_image)
else()
    message(FATAL_ERROR "SDL3_image is required to rasterise the shared SVG device icons")
endif()

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
function(lf2_embed_bytes symbol src output_list)
    set(out "${CMAKE_BINARY_DIR}/gen/${symbol}.c")
    if(NOT EXISTS "${src}")
        message(FATAL_ERROR "required asset ${src} is missing -- the binary cannot be built "
                            "without it")
    endif()
    file(READ "${src}" hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," body "${hex}")
    string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){16})" "\\1\n" body "${body}")
    file(WRITE "${out}"
         "/* Generated from ${src} by CMakeLists.txt -- do not edit, do not commit. */\n"
         "const unsigned char ${symbol}[] = {\n${body}\n};\n"
         "const unsigned int ${symbol}_len = (unsigned int)(sizeof ${symbol});\n")
    set(${output_list} ${${output_list}} "${out}" PARENT_SCOPE)
endfunction()

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/gen)
set(LF2_FONT_SOURCES "")
lf2_embed_bytes(lf2_font_sans ${CMAKE_SOURCE_DIR}/assets/fonts/LiberationSans-Regular.ttf LF2_FONT_SOURCES)
lf2_embed_bytes(lf2_font_mono ${CMAKE_SOURCE_DIR}/assets/fonts/LiberationMono-Regular.ttf LF2_FONT_SOURCES)
target_sources(lf2 PRIVATE ${LF2_FONT_SOURCES})

# Device art is shared by every port rather than copied into each tree. Consumers embed the
# SVG source at build time, so the installed game has no machine-specific runtime asset path.
set(PORT_ASSETS_DIR "$ENV{PORT_ASSETS_DIR}" CACHE PATH "shared port-assets checkout")
if(NOT PORT_ASSETS_DIR AND DEFINED ENV{SHARED_DIR})
    set(PORT_ASSETS_DIR "$ENV{SHARED_DIR}/port-assets")
endif()
if(NOT PORT_ASSETS_DIR)
    get_filename_component(PORT_ASSETS_DIR "${CMAKE_SOURCE_DIR}/../../shared/port-assets" ABSOLUTE)
endif()
set(LF2_DEVICE_ASSET_SOURCES "")
lf2_embed_bytes(lf2_device_keyboard_svg "${PORT_ASSETS_DIR}/sets/devices/keyboard.svg" LF2_DEVICE_ASSET_SOURCES)
lf2_embed_bytes(lf2_device_gamepad_svg "${PORT_ASSETS_DIR}/sets/devices/gamepad.svg" LF2_DEVICE_ASSET_SOURCES)
target_sources(lf2 PRIVATE ${LF2_DEVICE_ASSET_SOURCES})

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

# The hand-woven stage geometry loader (issue #62). It takes no game data -- the caller resolves
# a layer name to a span -- so it needs no guest and no GPU, only SDL's headers for MeshVertex.
# Run from tests/ so the fixtures resolve by a relative path that is the same everywhere.
# The stage light is PURE ARITHMETIC in a header, so it needs no SDL and no GPU -- which is the
# whole point: it used to live inside hd2d.c, where the only instrument was a screenshot.
add_executable(test_stagelight tests/test_stagelight.c)
target_include_directories(test_stagelight PRIVATE runtime)
target_compile_options(test_stagelight PRIVATE -Wall -Wextra)
target_link_libraries(test_stagelight PRIVATE m)
add_test(NAME stagelight COMMAND test_stagelight)

add_executable(test_stagegeom tests/test_stagegeom.c runtime/video/stagegeom.c)
target_include_directories(test_stagegeom PRIVATE runtime runtime/overrides runtime/video)
target_link_libraries(test_stagegeom PRIVATE SDL3::SDL3)
target_compile_options(test_stagegeom PRIVATE -Wall -Wextra)
add_test(NAME stagegeom COMMAND test_stagegeom ${CMAKE_SOURCE_DIR}/tests/fixtures/stages)

add_executable(test_texrect tests/test_texrect.c)
target_include_directories(test_texrect PRIVATE runtime/video)
target_link_libraries(test_texrect PRIVATE m)
target_compile_options(test_texrect PRIVATE -Wall -Wextra)
add_test(NAME texrect COMMAND test_texrect)

add_executable(test_texture_lru tests/test_texture_lru.c)
target_include_directories(test_texture_lru PRIVATE runtime/video)
target_compile_options(test_texture_lru PRIVATE -Wall -Wextra)
add_test(NAME texture_lru COMMAND test_texture_lru)

add_executable(test_device_icons tests/test_device_icons.c)
target_include_directories(test_device_icons PRIVATE runtime/ui)
target_compile_options(test_device_icons PRIVATE -Wall -Wextra)
add_test(NAME device_icons COMMAND test_device_icons)

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

add_executable(test_backdrop tests/test_backdrop.c)
target_include_directories(test_backdrop PRIVATE runtime/video)
target_compile_options(test_backdrop PRIVATE -Wall -Wextra)
add_test(NAME backdrop COMMAND test_backdrop)

add_executable(test_result_panel tests/test_result_panel.c runtime/video/result_panel.c)
target_include_directories(test_result_panel PRIVATE runtime/video runtime/overrides)
target_compile_options(test_result_panel PRIVATE -Wall -Wextra)
add_test(NAME result_panel COMMAND test_result_panel)

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
