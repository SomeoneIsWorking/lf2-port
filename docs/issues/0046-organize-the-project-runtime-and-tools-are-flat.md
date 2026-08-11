---
id: 46
title: Organize the project: runtime/ and tools/ are flat piles, and docs/ has a stale top-level note
status: resolved
symptom: runtime/ holds 38 files with no grouping (CPU, video, audio, Win32, app and unit tests all mixed), tools/ holds 31 (route tests, build helpers, RE tooling and extractors mixed), and docs/current-crash.md is a RESOLVED investigation whose name says it is current
tags: reported,workflow,structure,housekeeping
created: 2026-08-11
updated: 2026-08-11
---

REPORTED 2026-08-11: "properly organize the project".

WHY IT IS WORTH DOING RATHER THAN LEAVING ALONE. The port's three layers are stated clearly in
CLAUDE.md -- recompiler, runtime, overrides -- and the directory tree does not show them. A
newcomer, or a subagent given a brief, has to read 38 filenames to find where video lives.
`runtime/` currently mixes the guest CPU, the Win32 shim, the video path, audio, the app shell
and five unit-test programs at one level, and only `overrides/` is grouped.

TARGET LAYOUT (proposed; the point is grouping by WHAT THE CODE IS ABOUT, the same axis
CLAUDE.md already uses to explain the project):

    runtime/
      cpu/        guest.c guest.h guest_ops.h guest_map.h flags.c strops.c rwatch.c insn_test.h
      win32/      win32.c gdi.c imports.c com.c com.h wsock.c dshow.c
      video/      ddraw.c hostwin.h render.c render.h hd2d.c hd2d.h shaders/
      audio/      dsound.c mixer.c mixer.h
      input/      gamepad.c
      app/        main.c pause.c script.c script.h loadprof.c loadprof.h
      overrides/  unchanged -- it is already the one grouped thing here
    tests/        test_blit.c test_flags.c test_geom.c test_insn.c test_mixer.c
    tools/
      routes/     the eleven *_test.sh that boot the game, plus e2e.sh
      build/      build_shaders.sh build_matrix.sh scratch_clean.sh
      re/         ghidra/ ghidra_scripts/ decrypt_dat.py derive_entries.py check_arity.py
                  diff_data.py diff_trace.py find_path.py x87_profile.py bg_table_check.py
                  click_bands.py
      extract_game.py unpack_installer.py      -- the two a new user runs, kept at the top

WHAT THIS TOUCHES AND MUST BE UPDATED IN THE SAME COMMIT, or the move is worse than no move:
  - CMakeLists.txt source lists and include directories.
  - Every #include of a moved header, including runtime/overrides/*.c which reach up with
    "../guest_ops.h" and friends.
  - runtime/overrides/overrides.h's header comment, which is the map of which file provides
    which address and is kept current.
  - docs/codemap.md's Where column for every subsystem, and docs/running.md's path references.
  - tools/e2e.sh's list, tools/build/build_matrix.sh, run.sh, and each route script's internal paths.
  - CLAUDE.md's architecture diagram.

ALSO IN SCOPE, smaller:
  - docs/current-crash.md is a RESOLVED write-up (its first line says RESOLVED) sitting at the
    top of docs/ under a name that says it is current. A resolved investigation belongs in the
    issue catalog with its root cause, not as a top-level note -- that is what the catalog is
    for and it is where anyone would look. Move its content into an entry and delete the file;
    no tombstone.
  - tools/__pycache__ exists on disk. It is already gitignored, so this is only tidiness.

THE ORDER MATTERS: do NOT start this while other work is editing runtime/ or tools/. A file
move landing under an in-flight change produces conflicts that look like lost work. Land the
outstanding changes first, then move, then run the FULL check -- ctest plus every route in
tools/e2e.sh -- because a path this pervasive is exactly the kind of change that builds fine
and breaks a script nobody ran.

### Resolution (2026-08-11)
runtime/ and tools/ are now split by subsystem, and the include path change is what makes it
cheap rather than a churn of relative paths.

  runtime/cpu/     guest.c guest.h guest_map.h guest_ops.h flags.c strops.c rwatch.c insn_test.h
  runtime/win32/   win32.c gdi.c imports.c wsock.c com.c
  runtime/video/   ddraw.c render.c render.h shaders
  runtime/audio/   dsound.c mixer.c mixer.h
  runtime/input/   gamepad.c
  runtime/app/     main.c pause.c script.c loadprof.c
  tests/           every test_*.c
  tools/e2e.sh     stays at the top level -- it is the entry point, not a category
  tools/routes/    *_test.sh   tools/build/  build_*.sh scratch_clean.sh   tools/re/  ghidra + py

Every subdirectory is on the include path, so a header is spelled `#include "guest.h"` from
anywhere and moving a file again does not rewrite its includes. 26 `../X.h` forms went away.

VERIFIED on the moved tree, not asserted: build clean, ctest 8/8, and every route in
tools/e2e.sh -- smoke, mouse, resize, widescreen, background -- passing. background's
byte-identity arm at 794x550 is the one that matters here: it compares real frames against a
recorded native run, so a reorganization that silently dropped a source file or picked up a
stale object could not have passed it. codemap.py check is green on the new layout.

ONE REAL BREAK was found by running the scripts rather than by reading them: the moved
tools/build/*.sh used `dirname "$0"/..` to find the repo root, which resolved to tools/ once
they were a directory deeper. build_shaders.sh reported "no shaders in runtime/shaders" and the
shaders test failed. Both are `../..` now. That is exactly the failure this issue predicted --
"builds fine and breaks a script nobody ran" -- and it is the argument for the full e2e sweep
being part of the move rather than a follow-up.
