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
| S014 | Custom LAN multiplayer supports remote player inputs, on every interface including virtual LANs (ZeroTier, Tailscale) | partial | S005, plan 0129 hooks, Lucent rollback engine, Lucent interface enumeration | G002 |
| S015 | Representative gameplay conforms through the native/JIT product on each released host | missing | S005, S016 | G001, G002, G003 |
| S016 | Gameplay defaults to JIT, exposes no explicit interpreter mode, and accounts for bounded fallback by reason and coverage | partial | S018 | G001, G003 |
| S017 | Retired title-specific execution interfaces and toolchains remain absent | verified | — | G001 |
| S018 | `shared/x86port` supplies the x86-64 product JIT and bounded fallback contract | partial | — | G001, G003 |
| S019 | `shared/x86port` supplies a qualified ARM64 product JIT backend for macOS and Android | partial | — | G001, G003 |
| S020 | Asset-free Linux x86-64 native/JIT product CI runs from exact full-history inputs | partial | `.github/workflows/ci.yml` builds the product and runs focused boundary/quality tests; first remote run is pending landing | G003 |
| S021 | Browser WebAssembly runs the same native/JIT game with persistent imported files | partial | S005, S015 | G001, G003 |
| S022 | Internet multiplayer joins by ZeroTier code through the embedded Lucent ZeroTier node, with no install or account; play by code always works, and Lucent discovery (Nostr, a self-hosted lobby, Firebase) lists open sessions | partial | S014, Lucent ZeroTier and discovery components | G002 |
| S023 | Rollback netplay restores and resimulates exact game state | partial | S005, Lucent rollback engine, plan 0129 save/load/step/input hooks | G002 |

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
or bounded ZIP and validates the complete LF2 tree through a first-run screen.
That screen is `shared/setup-ui` drawn with RmlUi inside the port's own window,
the same module Benefactor shows, rather than a system message box: it asks for
one location, resolves and validates it before the port starts, and reports a
wrong choice on the screen so the player can simply choose again. Captured at
1280x720 (1x), 2340x1080 (landscape handset, density 3.0), and 1080x2340
(portrait) from the pinned checkout; `tests/test_setup_screen.cpp` exercises the
shipping config and validator, including the accept path against the fixture
executable identity.

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
Release run `34693726029` published the Apple Silicon `.app` ZIP alongside
the Linux AppImage and signed Android APK at `v0.1.10` from source `ab46291`.

Gap: issue #100's real Metal acceptance and the representative macOS
gameplay/release gate remain open despite the passing native/quality CI job.

### S013 — Android release and touch controls

The ARM64 package builder, landscape policy, private installer/folder/ZIP setup
behind the same shared setup screen the desktop shows,
touch routing, controller/touch presentation policy, updater, and signed-build
checks exist. The previous API24 arm64 debug APK assembled through the shared Android
prefix with NDK28.2 Clang and Java25. Its native ELF entry, packaged runtime
libraries, and exclusion of original game files pass inspection. Shared
`android-port` owns SAF staging and validated contained-directory publication.
A local Cuttlefish
ARM64 run installed that APK, staged the exact `game/` tree into app-private
storage, reached the retail mode menu through the ARM64 product JIT, and showed
the authored touch overlay; the process remained alive while presenting frames.

A previous clean local rebuild pinned `shared/android-port` at
`44fdd6c9dd35b66b81d3afa960c8e20d1e6e15f3` and produced
`build/release/LF2-Port-0.1.0-android-arm64-debug.apk`; its recorded FFmpeg
contract keeps AArch64 NEON enabled with hidden internal symbols.
The current source pin is `a1461d9b9014251ebe67b3f439e506488776942c`,
which moves the Activity, SAF importer, foreground progress, and service to
the shared Android framework and verifies a staged file's source prefix before
resuming an interrupted SAF copy. Hosted CI run `34691013242` built and inspected
an asset-free arm64-v8a APK with the earlier framework pin; its Linux and macOS jobs also passed.
The Lucent submodule now pins `885e9579f43ac9dcceb2fc64a0de7552f6cbb2cd`,
which removes its old Android Java; the LF2 logging and game-selection targets
build with Clang and both focused tests pass.

The asset-free Android package job passed in CI at run `34216452485` alongside
the Linux AppImage and Apple Silicon `.app` package jobs; this checks the build,
APK-content, and package-boundary paths only.
Release run `34693726029` published the `v0.1.10` arm64-v8a APK. Its published
SHA-256 matches `SHA256SUMS.txt`, its package version is `0.1.10`, and its
signing certificate matches the earlier v0.1.9 release identity
(`34db88707c35dba45cb073772cfbd5ffe08e0d44c13bebc36e589d01f9a38768`).

Gap: the run bypassed the system picker because the headless DocumentsUI surface
did not stay foregrounded, so interruption/recreation of a real SAF import is
still unverified. Signed physical-device correctness, audio/lifecycle checks,
and sustained performance evidence remain absent.

### S014 — Network play

Two LF2s meet by a pasted invite code over Lucent's peer-to-peer transport
(`runtime/netplay/online_session.cpp`, `lucent::online`): the path is
authenticated, a peer running a different `lf2.exe` is refused, the friend
takes the host's state (pages and calendar), and both play through `lucent::rollback`
with LF2 as its `Runtime` (`runtime/netplay/game_runtime.cpp`). Both players work the
port menu's ONLINE page (`runtime/ui/online_ui.cpp`): the host hosts and lists its game,
the friend joins it from the open games. `tools/routes/online_meeting_test.py` shows it
with two hidden instances from one install in separate rootless network namespaces
joined by one veth cable: equal final hashes on every reported frame, no desync.

In the same test the host starts a VS match, the friend's pad claims player 2,
and the friend's presses run player 2 across the stage on both screens
(`LF2_COOP_TRACK` and the kept frames), direct and over ZeroTier.

Gap: a host that played a match before hosting is unproven; host keys and mouse are withheld during one.
Desktop only: Android needs OpenSSL provisioned for `lucent::online`.

### S022 — Internet play by ZeroTier code

Shown: with the direct path off (`LF2_ONLINE_ZEROTIER_ONLY`), two LF2s meet by
invite code over their embedded ZeroTier nodes alone, through Lucent's lab root
(`tools/routes/online_meeting_test.py --zerotier`).

Gap: no meeting yet through ZeroTier's public roots between two real networks;
the invite is Lucent's long code, not yet the 10-hex-digit node ID; open games
are shown through a lab `lucent_lobby_server` only, not yet through public Nostr
relays or Firebase. A listed game's invite goes only to a player who asks
(`runtime/netplay/lobby_list.cpp`), and the page says the address goes with it.
A refresh keeps each still-listed game on its row; a game that leaves the list
still moves the rows below it up.
Play by code needs no discovery. The port embeds a ZeroTier node through the optional Lucent
component (libzt) on a controller-less ad-hoc network, so nobody installs an
app, makes an account or grants root or VPN permission; ZeroTier's roots relay
when no direct path exists. The session admits accepted codes only and checks
the executable identity. Not in the browser build. Design and phases:
`docs/plans/0129-online-multiplayer.md`.

### S023 — Rollback

Shown: snapshots through the guest write journal reload exactly
(`tools/routes/rollback_test.py`: every frame rerun from its snapshot matches a
plain run over 299 frames), and the online session runs on `lucent::rollback`:
with different scripted pads per player and a cable that delays and drops
packets, both peers rewind (about 100 to 200 frames resimulated) and keep equal
final hashes (`tools/routes/online_meeting_test.py`).

Online play rewinds through a VS match load, which creates COM surfaces, with
equal final hashes on both peers.

A rerun frame's sound calls are held back (`dsound_set_audible`): in the
two-player match a buffer restarted within 150 ms 2 and 1 times on the two peers,
against 23 and 37 without it.

Gap: a sound first heard only in a corrected frame is not played. Hooks
and design: `docs/plans/0129-online-multiplayer.md`.

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

The pinned `x86port` at `d2c2cf565d428b4616d89ea44f23afd142d893d8` supplies an
AArch64 backend and LF2 links it into an Android
APK. The Cuttlefish ARM64 run reached the retail mode menu through
`x86p_jit_engine_run`, with the native stack in `SDL_RenderPresent` and no crash;
this is product execution evidence rather than an APK-only inspection.

Gap: a representative interactive match, executable-memory/cache lifecycle
checks, and Apple Silicon qualification remain unverified. Bounded fallback
coverage cannot qualify the backend.

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

The Emscripten build now packages the native/JIT runtime with shared web-port's OPFS
staging and service-worker isolation resources. The local asset-free package
contains `lf2.js` and `lf2.wasm`; WebLua verified the setup page, secure
cross-origin isolation after the service-worker reload, persistent-storage
initialization, and the no-install state without console or network errors.
The browser package contains no game files. Source run `35441971535` built it at
`2bbd617`; the central `pages` deployment serves that artifact at
`https://someoneisworking.github.io/lf2-port/`. The old project Pages site is
disabled, and the live `publication.json` names the source run. WebLua verified
the central setup page, `crossOriginIsolated=true`, a WASM runtime, and no console
errors or failed network requests.

Browser gestures on the canvas are now claimed through shared web-port's
`claimCanvasGestures` rather than a lone `touch-action` declaration in this
page's stylesheet. Driving a real emulated touch device at 390x844 against the
packaged page, a twelve-step drag up the canvas leaves the document unscrolled
with `touch-action` and `user-select` both `none`; selection, the long-press
callout, the native bitmap drag and document overscroll are covered too, none of
which the local declaration handled. The canvas is sized with `100dvh` rather
than `100vh`, so its lower edge is not under a mobile address bar.

Gap: a real LF2 install has not yet been imported in a browser, so translated
gameplay, WebGPU presentation, and persisted-install restart remain unverified.
x86port must also qualify translated stores that modify cached/current code,
including an instruction-boundary exit from the active translation. Browser
gameplay remains open.
