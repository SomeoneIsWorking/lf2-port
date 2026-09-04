# Project state

This is the factual capability inventory for the intended LF2 native/JIT
product. Goals are in `docs/project-goals.md`; execution order and gates are in
`docs/migration.md`; atomic work is in `docs/issues/`.

## Comparison baseline

The user-facing baseline is the unmodified Windows release of *Little Fighter
2 v2.0a* running on Windows or through Wine: fixed-resolution 4:3 DirectDraw,
original keyboard/joystick configuration, and manual game-file setup.

The implementation baseline is the repository's previously verified native
host behavior: boot, menus, matches, audio, and host enhancements. Those
observations identify the frontier the native/JIT product must re-establish
independently.

## Current focus

S005 is the current focus: expand `shared/x86port` JIT coverage while
preserving the native entry, HLE, override, and host subsystem boundaries.

## Capability inventory

| ID | Capability or outcome | State | Factual dependency | Goals |
| --- | --- | --- | --- | --- |
| S001 | The intended product boots, navigates menus, and plays VS and Stage Mode with audio | partial | S005 | G001, G002 |
| S002 | High-resolution rendering preserves the game's pixel-art presentation | verified | — | G002 |
| S003 | Keyboard and controller actions are remappable and persist across runs | partial | — | G002 |
| S004 | The Linux AppImage provides no-terminal first-run game-file setup | partial | S005, S016 | G003 |
| S005 | Native overrides plus `x86port` JIT execute the authenticated LF2 image at runtime | partial | S018 | G001 |
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
| S016 | Gameplay defaults to JIT, exposes no explicit interpreter mode, and accounts for bounded fallback by reason and coverage | partial | S018 | G001, G003 |
| S017 | Retired title-specific execution interfaces and toolchains remain absent | verified | — | G001 |
| S018 | `shared/x86port` supplies the x86-64 product JIT and bounded fallback contract | partial | — | G001, G003 |
| S019 | `shared/x86port` supplies a qualified ARM64 product JIT backend for macOS and Android | missing | — | G001, G003 |

## Capability details

### S001 — Playable game flow

The pre-migration product reached boot, menus, character selection, VS matches,
Stage Mode, sound effects, and WMA background music.

Gap: none of that frontier has been re-established through the intended JIT
product. Earlier observations are not current product evidence.

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
the JIT-default product and reporting bounded fallback coverage.

### S005 — Native/JIT execution

The x86-64 product now adapts the existing guest memory/PE state, imports, COM,
and native overrides to `x86port_runtime`. Native calls use runtime addresses;
scoped original calls disable only their current override. A bounded silent run
loaded the authenticated image, entered the native startup, created the host
window and DirectDraw surface, completed data/art/music initialization, presented
frame 1, reached the mode menu, entered 698,084 blocks while translating 530,993
instructions across 9,218 distinct blocks, and then refused at `0x0040000C`
because control entered PE/DOS-header data rather than an instruction.

Gap: recover the control-flow owner that entered `0x0040000C` and re-establish
the complete gameplay frontier. Issue #128 owns this work.

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

The ARM64 package builder, landscape policy, private installer/folder/ZIP setup,
touch routing, controller/touch presentation policy, updater, and signed-build
checks exist.

Gap: `x86port` has no qualified ARM64 product backend, and signed physical-device
correctness, audio, lifecycle, and sustained performance evidence remains absent.

### S014 — Network play

Missing capability: the original network mode is not ported; the current HLE
surface reports that no network is available.

### S015 — Representative gameplay conformance

Missing capability: pass the bounded Stage Mode restoration scenario in
`docs/migration.md`, including CPU/memory state, timing/interrupts, service
events, rendering, audio, native overrides, scoped original calls, denominated
JIT coverage, controlled negatives, and released-host performance.

### S016 — JIT-default execution policy

Evidence: LF2's consumer CMake requires `x86port_runtime`, and x86port does not
expose its top-level-only `x86port_test_oracle` target to this build. The live
unsupported-instruction run aborted because the pinned runtime has no bounded
fallback contract. The Clang-built x86-64 product symbol audit inspected 8,972
symbol lines, found the required LF2/JIT entry points, and excluded the test
oracle; its controlled negative rejects that oracle symbol.

Gap: add and pass JIT-default selector, reason-coded fallback, and coverage
audits on every release configuration and architecture. Fallback execution
does not prove gameplay or performance for the affected region.

### S017 — Retired-interface removal

Evidence: the source tree contains only the native/JIT execution model.
`tests/test_source_policy.py` scans all first-party source, documentation, and
tools for the exact retired interfaces and proves the negative path.
`tests/test_execution_boundary.py` requires the adapter, `x86port_runtime`, and
the typed native-address registry; removing the adapter fails the policy.

### S018 — x86-64 product JIT

The `x86port_runtime` product library is integrated and supplies runtime
interception, code invalidation, denominated block statistics, and explicit
unsupported-instruction refusal without exposing the test oracle. LF2 owns the
typed native-address table and scoped original-call policy.

Gap: the first reached LF2 failure is control entering non-code PE/DOS-header
data at `0x0040000C`. The shared runtime also lacks the bounded fallback
contract. Full gameplay and conformance remain unverified.

### S019 — ARM64 product JIT

Missing capability: `x86port` needs an ARM64 backend with W^X publication,
instruction-cache coherence, ABI transitions, invalidation, and representative
LF2 gameplay qualification before macOS ARM64 or Android can ship the new
product. Bounded fallback coverage cannot qualify the backend.
