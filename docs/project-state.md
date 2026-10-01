# Project state

One row per capability. `verified` / `partial` / `blocked` / `missing`. Goals
are in `docs/project-goals.md`, execution order in `docs/migration.md`, atomic
work in `docs/issues/`, and which native replacements are grounded in the
binary in `docs/re-frontier.md`.

**Comparison baseline:** the unmodified Windows release of *Little Fighter 2
v2.0a* on Windows or Wine — fixed 4:3 DirectDraw, original keyboard/joystick
configuration, manual game-file setup. Every user-visible delta (widescreen,
controller joining, host text, display modes, browser and Android delivery) is
its own row below.

**Current focus:** expand `shared/x86port` JIT coverage so the authenticated LF2
image runs through the product JIT (issue #128).

| ID | Capability | State | Evidence or gap |
| --- | --- | --- | --- |
| S001 | Boot, menus, character select, VS and Stage Mode with audio | partial | Ten-frame silent Clang run reached the mode menu through the product JIT (734,144 blocks, zero refusals); no match re-established |
| S002 | High-resolution rendering keeps pixel art crisp | verified | Native renderer presents at modern output resolutions with nearest-filtered sprites |
| S003 | Remappable keyboard/controller actions persist | partial | Bindings and RmlUi menu persist; no physical-controller run through the JIT product |
| S004 | Linux AppImage first-run game-file setup without a terminal | partial | Installer/tree/exe/ZIP selection validated through the shared setup screen; no clean-machine install-and-play gate |
| S005 | Native overrides plus `x86port` JIT run the authenticated image | partial | `x86port_runtime` linked, scoped original calls and recursive-context restore covered; gameplay frontier not re-established (#128) |
| S006 | Widescreen/ultrawide widen the view instead of stretching | verified | 16:9 and ultrawide captures show extra stage area with preserved source geometry |
| S007 | Character lighting and cast shadows configurable | verified | Native renderer and RmlUi settings expose both controls |
| S008 | Two physical controllers join as two local players | partial | Second pad auto-assigns to Player 2; no current hardware run |
| S009 | Controller connect/disconnect/reconnect | partial | Device owner handles events with stable bindings; no hardware evidence |
| S010 | Borderless, windowed, fullscreen, Alt+Enter | verified | Native window/display path owns the switching |
| S011 | Host text is anti-aliased | verified | SDL_ttf with embedded Liberation fonts; original bitmap panels stay original |
| S012 | macOS native/JIT build and Metal renderer release-qualified | partial | Packaged and CI-built; real Metal acceptance and macOS gameplay gate open (#100) |
| S013 | Android ARM64 package with touch controls and private setup | partial | Signed APK published at v0.1.10, Cuttlefish reached the mode menu; real SAF interruption and physical-device evidence absent |
| S014 | LAN multiplayer carries remote inputs | missing | Wire contract and loopback socket exist; discovery, session lifetime, input injection do not |
| S015 | Representative gameplay conforms on each released host | missing | Bounded Stage Mode restoration scenario in `docs/migration.md` not run |
| S016 | Gameplay defaults to the JIT with no interpreter mode and bounded fallback | partial | Product links `x86port_runtime` and never selects the test oracle; no bounded fallback contract in the shared runtime |
| S017 | Retired title-specific execution interfaces stay absent | verified | `tools/build/check_source_policy.py` fails on any retired interface reappearing |
| S018 | `x86port` supplies the x86-64 product JIT | partial | Runtime interception, invalidation, block statistics and explicit unsupported-instruction refusal work; bounded fallback missing |
| S019 | `x86port` supplies a qualified ARM64 product JIT | partial | AArch64 backend runs to the mode menu on Cuttlefish; Apple Silicon qualification and cache-lifecycle checks open |
| S020 | Asset-free Linux x86-64 CI builds the product and runs the quality gate | partial | Workflow builds the real product and runs game data, source policy, structure, format, lint and selection tests from full-history inputs |
| S021 | Browser WASM runs the same native/JIT game with persistent files | partial | Asset-free package deployed to Pages; WebLua verified isolation, storage and gestures, no real LF2 import yet |
| S022 | Internet multiplayer discovers sessions and plays peer-to-peer | missing | No Firebase signaling or peer channel |
| S023 | Rollback netplay restores and resimulates exact state | missing | No complete snapshot, restore, or deterministic frame step |
