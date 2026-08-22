# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

# LF2 port — working agreement

Native Linux/macOS port of **Little Fighter 2 v2.0a** by *static recompilation*: the 32-bit
x86 game binary is translated to C at build time, and the Win32/DirectX APIs it calls are
reimplemented on SDL3.

## Read these before starting anything non-trivial

| Doc | What it answers |
|---|---|
| `docs/codemap.md` | per-subsystem status, the binary's layout, the platform boundary, and the write-ups of every hard-won fix |
| `docs/running.md` | every way to run the port: headless, scripted input, screenshots, and the full table of `LF2_*` debug switches |
| `docs/issues/` | the work queue and every past dead end (see below) |
| `docs/info/claims/`, `docs/info/instruments/` | what was *proven* and with what evidence; which tools can be trusted |

Do not re-derive an address, an offset, or a menu structure from the disassembly before
grepping these — nearly all of them are already recorded with the measurement that found them.

## Build, run, test

```sh
# One-time: the game tree is NOT in the repo and the build needs game/lf2.exe.
curl -O https://lf2.net/LF2_v2.0a.exe
python3 tools/extract_game.py LF2_v2.0a.exe game/

python3 tools/build/build.py
cd game && ../scratch/build-clang/lf2 lf2.exe      # cwd MUST be the game tree — data is opened by relative path
./run.sh                                     # extract-if-needed + build + run, from anywhere
```

Build artefacts go in the gitignored `scratch/`, never `/tmp`.

```sh
ctest --test-dir scratch/build-clang                 # offline suite + Clang gates, about 15 s
tools/e2e.py                                   # the scripts that boot the game (minutes)
tools/e2e.py mouse render                      # one or more of them by name
```

- **`ctest` is one suite and it is fast.** Nothing in it boots the game; the gameplay checks
  stay sub-second, and the two-source Clang lint gate keeps the whole set around fifteen seconds.
  That bar is the point: a suite with a
  five-minute test in it stops being run, which is how the mouse route stayed green and broken
  for as long as it existed (issue #26).
- **A claim that can be checked offline must be.** `runtime/overrides/geom.h` holds the port's
  pure geometry — the composition width, the parallax, the camera bounds and the wide-view
  centring, the overlay's rows, the stereo pan — and `tests/test_geom.c` walks it in a
  millisecond. The overrides *include* that header, so the test is not exercising a copy. The
  audio pan moved this way: a three-run, 270-second script became 20 assertions.
- **`tools/e2e.py` is for what genuinely needs a running game** — whether a route reaches a
  screen, whether a second pad drives its fighter, whether the GPU renderer matches the
  software one. It runs them one at a time; each wraps its instance in a wall-clock `timeout`
  and two instances on one machine trip it.
- Tests exit **77 to SKIP** (no game tree, no Ghidra dump, non-x86 host). A skip is not a pass;
  read the output.
- `decoder_corpus` needs `re/instructions.tsv`, which is gitignored — regenerate from your own
  copy per `docs/isa-scope.md`. The other instruction tests derive a substitute corpus.

## Architecture — the three layers, and where a change belongs

```
  re/entries.tsv  +  game/lf2.exe
        |  recompiler/lift.c  (x86 -> C, using recompiler/x86_decode.c)
        v
  scratch/build-clang/gen/lf2_recomp.c        the game's own logic, machine-generated, never edited
        |  calls fn_<addr>() and the imports below
        v
  runtime/{cpu,win32,video,audio,input,app}/   the platform: guest CPU/memory +
                                              Win32/DirectX on SDL3
  runtime/overrides/*.c                 hand-written fn_<addr>() replacing recompiled functions
```

**1. The recompiler (`recompiler/`).** `lift.c` decodes the whole `.text` and emits C for
every function listed in `re/entries.tsv`. The generated file is build output — a bug in the
game's behaviour that traces to a mistranslated instruction is fixed in `lift.c` and shows up
everywhere at once (the ADC/SBB carry bug in `docs/codemap.md` is the worked example). The
game's four monolithic functions (28/20/18/15 KB — main loop, character state machine) exist
*only* as recompiled code; hand-porting them is not on the table.

**2. The runtime (`runtime/`), grouped by what the code is about** (issue #46):

| Directory | What lives there |
|---|---|
| `runtime/cpu/` | guest CPU state, lazy flags, the 4 GiB lazily-committed address space, PE loading and import binding — `guest.c/.h`, `guest_ops.h`, `flags.c`, `strops.c`, `rwatch.c`, and `guest_map.h`, where the arena layout is declared once with build-time overlap checks (a surface arena that overran the sound arena once made the game play bitmaps as audio) |
| `runtime/win32/` | the Win32 shim — `win32.c` (window, message pump, input, window modes), `gdi.c` (text), `imports.c` (the CRT and the guest clock), `com.c` (the DirectDraw vtables), `dshow.c`, `wsock.c` (netplay, stubbed) |
| `runtime/video/` | `ddraw.c` (DirectDraw → SDL3), `render.c`, `engine.c` + `engine_textures.c` (native rendering and its frame-safe texture cache), `hd2d.c` (lighting), `hostwin.h` |
| `runtime/audio/` | `dsound.c` + `mixer.c` |
| `runtime/input/` | device state and persistent action bindings — `gamepad.c/.h`, `keyboard.c/.h`, `bindings.c/.h` |
| `runtime/app/` | the port's own shell — `main.c`, `pause.c`, `script.c` (scripted input), `loadprof.c` |
| `runtime/overrides/` | see below |
| `tests/` | the unit tests, which are programs rather than runtime code |

Every one of those directories is on the include path, so a file says `#include "guest_ops.h"`
wherever it happens to sit. That is deliberate: relative `../` includes tie each file to its
position in the tree and make any future regrouping an edit of every header line.

Only ~130 imported symbols exist, so this is the *entire* porting surface. But note
`DirectDrawCreate` is DDraw's only import: every other video call reaches the game through a
COM vtable the recompiler cannot resolve statically, and `runtime/win32/com.c` supplies those.

## Host structure follows Dusklight

Dusklight is the architecture reference for host-side ownership. LF2 adapts that pattern to SDL3 and
static recompilation rather than copying Dusklight's platform implementations:

- `runtime/app/` composes lifecycle and startup policy.
- `runtime/ui/` owns the RmlUi document, device-independent UI input translation, and SDL
  render backend as separate modules (`settings_ui.cpp`, `rmlui_input.cpp`,
  `rmlui_backend.cpp`). It also owns the port-authored pre-fight visual layout and native
  font raster in `overlay_panel.c`; `overrides/text.c` only identifies the guest producer
  painter slot before the shared draw helper loses it.
- `runtime/input/` owns device discovery and persistent action bindings; config only stores values.
- `runtime/video/`, `runtime/audio/`, and `runtime/win32/` remain cohesive peer subsystems.
- `runtime/overrides/` changes game behavior and does not absorb host platform mechanisms.

Generic port UI art comes from `PORT_ASSETS_DIR`, `SHARED_DIR/port-assets`, or the standard sibling
`../../shared/port-assets`, never from a copied LF2-local version. The settings screen and in-game
device indicators embed that repository's SVG icons at build time, so the installed game has no
host checkout path.

`tools/build/check_structure.py` enforces the boundary: new runtime source files are capped at 1,200
lines and existing files above that limit may not grow. At 2,000 lines a file is critical extraction
work, not merely large. Lower a legacy cap when extracting code; never raise one merely to land a
feature. Update this section and `docs/codemap.md` whenever ownership moves.

**3. The overrides (`runtime/overrides/`).** Listing a hex address in `re/overrides.txt`
excludes it from lifting; the generated code still calls `fn_<addr>()` and the linker resolves
it to a hand-written C function in `runtime/overrides/`. These run in the **guest ABI** —
arguments on the guest stack, stdcall callee pops. `overrides.h`'s header comment is the map of
which file provides which address, and is kept current.

The split is by *what the code is about*, not by which address it replaces — one screen's
behaviour spreads over several overrides and `fn_0043f010` alone draws every screen. The line
worth preserving: `coop.c` is what the game does, `coop_debug.c` is how this port knows it did
it. A probe that grows into a mechanism moves across; a mechanism that only exists to be
measured never belonged in `coop.c`.

**Change behaviour where the game expresses it — an override — rather than intercepting the
consequences at the Win32 boundary.** That is why the port has real drop-in coop, a pause menu
and a live-resizing widescreen rather than a shim's approximation of them.

`tools/` is grouped too: `tools/routes/` holds the scripts that boot the game (run them with
`tools/e2e.py`), `tools/build/` the build helpers, `tools/re/` the Ghidra scripts and the
memory/trace diff tools. The two a new user actually runs — `extract_game.py` and
`unpack_installer.py` — stay at the top.

## The active queue is `docs/issues/`, and it is the answer to "work on the active issues"

```sh
python3 ~/.Codex/skills/issue-catalog/catalog.py list --tag reported --status open   # THE QUEUE
python3 ~/.Codex/skills/issue-catalog/catalog.py list --status open                  # everything open
python3 ~/.Codex/skills/issue-catalog/catalog.py show <id>
```

- **"work on the active issues" / "the issues" / "what's open" means exactly the first
  command.** Run it before asking what to do next, and work the entries in it. Do not invent
  a separate list, a TODO file, or an in-conversation plan that outlives the conversation —
  there is one queue and it is in the repo, so it survives compaction and reaches subagents.

- **Anything the user REPORTS gets filed the moment it is reported**, before the fix is
  attempted, tagged `reported`. A bug seen in play, a design objection ("that shouldn't be
  behind an env var"), a "this should work differently" — all of it. Filing first is what
  stops a report being lost when the session turns to something else, and it is what makes
  the queue an honest picture of what is owed.

- **File the CAUSE and the constraint, not just the symptom.** An entry that says only what
  went wrong makes the next session re-derive the investigation. Say what was measured, what
  is still unknown, and — where it applies — which obvious fix is WRONG and why, so nobody
  ships it. Issue #19 is the shape to copy.

- **Close entries by editing them, not by leaving them.** `catalog.py resolve <id> "<what
  actually fixed it>"`, in the same commit as the fix. A resolved entry with the real cause
  in it is worth more than a closed one that says "done".

## Verification, and what a red test does and does not mean

- **A failing route-scripted test is not evidence of a regression until it has been
  reproduced against a stashed or committed tree.** These tests drive the game through its
  menus, and the routes were frame-numbered, so a busy machine moved them (issue #18). Two
  failures were nearly attributed to a change that had nothing to do with them.

- **Prefer `button@match+30` to `button:2300` in pad scripts.** The `@<screen>` form fires
  off the game's own drawing (`charselect`, `overlay`, `match`), so a press aimed at the
  match lands in the match however long the data load took.

- **A scripted run ignores physical controllers** — an attached pad binds gamepad slot 0 and
  silently stalls every route test at the front end. Unplug before running the pad tests.

- **Never claim a green that was not run.** If the thing that would verify a change is
  itself broken, say so and leave the work uncommitted or the commit message honest.

## No black-box debugging — the answer is in the decompilation

The binary is right there and `tools/re/ghidra_scripts/DecompDump.py` dumps any function to
`scratch/decomp/` in seconds (`LF2_DECOMP_TARGETS`, invocation in `docs/running.md`). So a
question about what the game does is answered by READING THE BRANCH, not by inferring it from
outside.

- **Banned: write-a-word-and-watch.** `LF2_EXIT_PROBE` diffed `.data` between two screens and
  spent a run per candidate; six came back negative. The answer was three lines of
  `fn_00431d10`. It is deleted.
- **Banned: reading a sampled `.data` sequence as a state machine.** `0044d020` going
  `1 -> 10 -> 3 -> 1` was written up as "the game walks there on its own". It does not — that
  pair of writes is one confirm press, and only the code can say so (issue #22).
- **Banned: a frame dump as the evidence for a state question.** Screens that share a blit
  destination can share a picture.

Measure ONE thing afterwards to confirm the read. Observation confirms an RE finding; it never
substitutes for one. Issue #61.

## Env vars are for DIAGNOSTICS, never for features

A feature nobody can find is not a feature. Drop-in coop was once behind `LF2_COOP=1` and had
to be turned on to exist; widescreen was once set by `LF2_WIDESCREEN` read at startup instead
of following the window (issue #20). Both are now unconditional and derived from real state.
The `LF2_*` namespace is for probes, dumps, traces and test scaffolding — if a player would
want it, it belongs in the game's own behaviour or its menus.

## No distributable game content is shipped here

`game/`, `LF2_v2.0a.exe`, `scratch/` and `re/instructions.tsv` are gitignored and must stay
that way — the last is Ghidra's dump *with raw bytes*, from which `.text` can be rebuilt.
`re/entries.tsv` and `re/functions.tsv` (addresses, sizes, names, no code) are committed.
Never commit an extracted asset, executable, or anything from which shipped game content can be
reconstructed. Curated promotional screenshots under `docs/screenshots/` are the sole visual-output
exception: they may show the running game, but must be intentional README/documentation captures,
not an archival frame-dump corpus.
