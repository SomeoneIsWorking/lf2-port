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
| S019 | `shared/x86port` supplies a qualified ARM64 product JIT backend for macOS and Android | partial | — | G001, G003 |
| S020 | Asset-free Linux x86-64 native/JIT product CI runs from exact full-history inputs | partial | `.github/workflows/ci.yml` builds the product and runs focused boundary/quality tests; first remote run is pending landing | G003 |
| S021 | Browser WebAssembly runs the same native/JIT game with persistent imported files | partial | S005, S015 | G001, G003 |

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
scoped original calls disable only their current override. Recursive calls now
restore the enclosing interception context, with a production-adapter regression
covering ordinary calls, nested HLE and scoped originals. A ten-frame silent
Clang run against x86port `e1522b2` completed startup and reached the mode menu,
then exited normally with 734,144 entered JIT blocks and zero refusals.
Issue #128 records the exact discriminator and remaining conformance boundary.

Gap: re-establish the complete gameplay frontier and independent conformance.

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

The native macOS build and Metal shader path exist. `tools/build/macos.py`
stages the native binary, its non-system dylib closure, `stages/`, and an
ad-hoc signed `.app`; the release workflow now exercises that packager on an
Apple Silicon runner. The combined Linux, Android, and macOS ARM64 CI run
`34214371352` passed the native/JIT and quality gates on all three hosts.

Gap: issue #100's real Metal acceptance and the representative macOS
gameplay/release gate remain open despite the passing native/quality CI job.

### S013 — Android release and touch controls

The ARM64 package builder, landscape policy, private installer/folder/ZIP setup,
touch routing, controller/touch presentation policy, updater, and signed-build
checks exist. The API24 arm64 debug APK assembles through the shared Android
prefix with NDK28.2 Clang and Java25. Its native ELF entry, packaged runtime
libraries, and exclusion of original game files pass inspection. Lucent owns
SAF staging and validated contained-directory publication. A local Cuttlefish
ARM64 run installed that APK, staged the exact `game/` tree into app-private
storage, reached the retail mode menu through the ARM64 product JIT, and showed
the authored touch overlay; the process remained alive while presenting frames.

A clean local rebuild pinned `shared/android-port` at
`6735dc557b2ae56d735ce8140867b8375567d9e6` and produced
`build/release/LF2-Port-0.1.0-android-arm64-debug.apk`; its recorded FFmpeg
contract keeps AArch64 NEON enabled with hidden internal symbols.

The asset-free Android package job passed in CI at run `34216452485` alongside
the Linux AppImage and Apple Silicon `.app` package jobs; this checks the build,
APK-content, and package-boundary paths only.

Gap: the run bypassed the system picker because the headless DocumentsUI surface
did not stay foregrounded, so interruption/recreation of a real SAF import is
still unverified. Signed physical-device correctness, audio/lifecycle checks,
and sustained performance evidence remain absent.

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

Gap: ten-frame startup evidence now passes; full gameplay and conformance
remain unverified. The shared direct engine also lacks the bounded fallback
contract.

### S019 — ARM64 product JIT

The pinned `x86port` supplies an AArch64 backend and LF2 links it into an Android
APK. The Cuttlefish ARM64 run reached the retail mode menu through
`x86p_jit_engine_run`, with the native stack in `SDL_RenderPresent` and no crash;
this is product execution evidence rather than an APK-only inspection. A
representative interactive match, executable-memory/cache lifecycle checks, and
Apple Silicon qualification remain unverified. Bounded fallback coverage cannot
qualify the backend.

### S020 — Asset-free CI

The workflow builds LF2's real Linux x86-64 native/JIT product and runs focused
execution-boundary, product-symbol, configuration, structure, format, and lint
tests from exact full-history source inputs with read-only repository
permissions and no `lf2.exe`. Its Android arm64-v8a job now assembles the
asset-free debug APK through `tools/build/android.py`, the pinned Android
profile, and the shared Android dependency prefix; it does not claim device
runtime evidence. The first remote Linux and Browser runs passed at
`ad01e8d` (CI run 34197382110; Pages run 34197382067), and the combined Linux,
Android, and macOS ARM64 package run passed at `34216452485`.

Gap: Android device/runtime qualification remains open; the Android package
and local Cuttlefish evidence are recorded in S013 and S019. Windows is the
comparison baseline rather than an intended shipping host and is therefore
inapplicable to this port's current delivery goals.

### S021 — Browser native/JIT delivery

The Emscripten build now packages the native/JIT runtime with Lucent's OPFS
staging and service-worker isolation resources. The local asset-free package
contains `lf2.js` and `lf2.wasm`; WebLua verified the setup page, secure
cross-origin isolation after the service-worker reload, persistent-storage
initialization, and the no-install state without console or network errors.
The package is ready for the GitHub Pages workflow and never contains game
files. The Browser release workflow deployed the asset-free package at
`https://someoneisworking.github.io/lf2-port/` (run 34216936080).

Gap: a real LF2 install has not yet been imported in a browser, so translated
gameplay, WebGPU presentation, and persisted-install restart remain unverified.
x86port must also qualify translated stores that modify cached/current code,
including an instruction-boundary exit from the active translation. Pages
deployment is verified by run `34216936080`; browser gameplay remains open.
