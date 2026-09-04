# Project state

This is the factual capability inventory for the intended LF2 native/JIT
product. Goals are in `docs/project-goals.md`; execution order and gates are in
`docs/migration.md`; atomic work is in `docs/issues/`.

## Comparison baseline

The user-facing baseline is the unmodified Windows release of *Little Fighter
2 v2.0a* running on Windows or through Wine: fixed-resolution 4:3 DirectDraw,
original keyboard/joystick configuration, and manual game-file setup.

The implementation baseline is this repository's previously verified native
host plus offline-generated x86-to-C guest execution. That execution method is
retired and must not be built or run for new evidence. Its durable behavioral
evidence remains useful only to identify the frontier the native/JIT product
must re-establish independently.

## Current focus

S005 is the current focus: replace offline guest-code execution with the
`shared/x86port` JIT while preserving the native entry, HLE, override, and host
subsystem boundaries.

## Capability inventory

| ID | Capability or outcome | State | Factual dependency | Goals |
| --- | --- | --- | --- | --- |
| S001 | The intended product boots, navigates menus, and plays VS and Stage Mode with audio | partial | S005 | G001, G002 |
| S002 | High-resolution rendering preserves the game's pixel-art presentation | verified | — | G002 |
| S003 | Keyboard and controller actions are remappable and persist across runs | partial | — | G002 |
| S004 | The Linux AppImage provides no-terminal first-run game-file setup | partial | S005, S016 | G003 |
| S005 | Native overrides plus `x86port` JIT execute the authenticated LF2 image with no offline guest-code generation | missing | S018 | G001 |
| S006 | Widescreen and ultrawide expand the visible stage instead of stretching the 4:3 picture | verified | S002 | G002 |
| S007 | Native character lighting and cast shadows are configurable in the port menu | verified | S002 | G002 |
| S008 | Two physical controllers join as two local players without manual slot setup | partial | S003 | G002 |
| S009 | Controllers connect, disconnect, and reconnect without restarting the game | partial | S003 | G002 |
| S010 | Borderless, windowed, fullscreen, and Alt+Enter display switching work | verified | S002 | G002 |
| S011 | Menus and character selection use modern anti-aliased host text | verified | — | G002 |
| S012 | The macOS native/JIT build and Metal renderer are release-qualified | partial | S005, S019 | G003 |
| S013 | The Android ARM64 native/JIT build provides touch controls and private installer/folder/ZIP setup | partial | S005, S019 | G003 |
| S014 | Network play from the original game is available natively | missing | S005 | G002 |
| S015 | Representative gameplay conforms through the native/JIT product on each released host | missing | S005, S016 | G001, G002, G003 |
| S016 | Gameplay build/link/selector surfaces contain no interpreter execution, interpreter-backed helper, or fallback machinery | missing | S018 | G001, G003 |
| S017 | Offline translator, generated corpus, static dispatcher, generation-only seeds, and static-only tests are absent | missing | S015, S016 | G001 |
| S018 | `shared/x86port` supplies an x86-64 product JIT mode with no interpreter/helper fallback | missing | — | G001, G003 |
| S019 | `shared/x86port` supplies a qualified ARM64 product JIT backend for macOS and Android | missing | — | G001, G003 |

## Capability details

### S001 — Playable game flow

The pre-migration product reached boot, menus, character selection, VS matches,
Stage Mode, sound effects, and WMA background music.

Gap: none of that frontier has been re-established through the intended JIT
product. The previous generated-C runs are not current product evidence.

### S002 — High-resolution presentation

Evidence: the native renderer presents at modern output resolutions while
keeping game sprites nearest-filtered; focused renderer routes and curated
captures establish the owning host subsystem independently of CPU translation.

### S003 — Persistent input bindings

The native input path and RmlUi port menu implement device-independent keyboard
and controller actions plus persistent bindings.

Gap: representative physical-controller behavior still needs end-to-end
verification on shipping hardware and then re-conformance in the JIT product.

### S004 — Linux AppImage setup

The native setup path accepts the original installer, extracted tree, executable,
or bounded ZIP and validates the complete LF2 tree through a first-run dialog.

Gap: the package still needs a clean-machine install-and-play gate containing
the JIT product and proving interpreter absence.

### S005 — Native/JIT execution

Missing capability: adapt the existing guest memory, PE mapping, imports, and
native overrides to `shared/x86port`; execute every non-native guest path from
live authenticated bytes through its JIT; and remove every offline translation
step. Issue #128 owns this migration.

### S006 — Widescreen and ultrawide

Evidence: 16:9 and ultrawide match captures show additional stage area with
preserved source geometry, and focused routes exercise the native view, camera,
parallax, backdrop, and walk-bound owners.

### S007 — Lighting and shadows

Evidence: the native renderer and RmlUi settings expose character lighting and
cast-shadow controls through the running host presentation path.

### S008 — Two-controller local play

The native input owner supports four controller slots and automatically assigns
a second pad to Player 2.

Gap: the two-controller path lacks a current physical-hardware run and must then
pass through the JIT gameplay product.

### S009 — Controller hot-plug

The native device owner handles connect, disconnect, and reconnect events with
stable action bindings.

Gap: the lifecycle lacks representative physical-controller evidence and must
then pass through the JIT gameplay product.

### S010 — Window modes

Evidence: borderless, windowed, fullscreen, and Alt+Enter switching are owned by
the native window/display path and have focused route coverage.

### S011 — Anti-aliased text

Evidence: native menu and character-selection text uses SDL_ttf with embedded
redistributable fonts; original bitmap-authored game panels remain original.

### S012 — macOS release

The native macOS build and Metal shader path exist.

Gap: issue #100's real Metal acceptance remains open, and no ARM64 JIT product
has passed the representative gameplay/release gate.

### S013 — Android release and touch controls

The ARM64 package shell, landscape policy, private installer/folder/ZIP setup,
touch routing, controller/touch presentation policy, updater, and signed-build
checks exist.

Gap: `x86port` has no qualified ARM64 product backend, and signed physical-device
correctness, audio, lifecycle, and sustained performance evidence remains absent.

### S014 — Network play

Missing capability: the original network mode is not ported; the current HLE
surface reports that no network is available.

### S015 — Representative gameplay conformance

Missing capability: pass the bounded Stage Mode retirement scenario in
`docs/migration.md`, including CPU/memory state, timing/interrupts, service
events, rendering, audio, native overrides, scoped original calls, denominated
JIT coverage, controlled negatives, and released-host performance.

### S016 — Interpreter exclusion

Missing capability: provide gameplay build, link-map/symbol, and selector audits
that prove no interpreter, interpreter-backed helper, fallback route, or engine
choice is compiled into the product. A zero fallback counter alone is
insufficient.

### S017 — Static-path removal

Missing capability: after S015 and S016 pass, delete the offline translator,
generated source build rules, generated-symbol dispatcher, generation-only
entry/override seeds, static-only tests, and stale documentation in the same
milestone. Do not retain a compatibility mode or historical implementation
directory.

### S018 — x86-64 product JIT

Missing capability: consume an `x86port` x86-64 execution target whose product
library omits interpreter execution and interpreter-backed instruction helpers,
refuses unsupported instructions by guest PC, and exposes runtime interception,
scoped original calls, code invalidation, and denominated block statistics.

### S019 — ARM64 product JIT

Missing capability: `x86port` needs an ARM64 backend with W^X publication,
instruction-cache coherence, ABI transitions, invalidation, and representative
LF2 gameplay qualification before macOS ARM64 or Android can ship the new
product. Interpretation is not a fallback.
