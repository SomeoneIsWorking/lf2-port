# LF2 port — codemap

This map owns responsibility and placement only. Capability state belongs in
`docs/project-state.md`, product intent in `docs/project-goals.md`, execution
order and acceptance gates in `docs/migration.md`, and evidence in the issue,
claim, instrument, and RE-frontier registries.

## Architecture

```text
player-owned LF2 image and tree
              |
runtime/app: identity, selection, native entry, lifecycle, configuration
              |
runtime/cpu LF2 adapter ---------> shared/x86port product JIT
      |                                  |
      |                                  +-- live guest blocks/code cache
      |                                  +-- bounded, counted fallback on JIT failure
      +-- title interception policy
            |                 |
            |                 +-- runtime/overrides: native game behavior
            +-- runtime/win32: Win32/CRT/COM/HLE services
                                      |
             video | audio | input | UI | platform/package owners

separately built test target: same state/memory seam + x86port oracle
```

The existing native host is preserved. CPU execution enters the current HLE,
DirectDraw, audio, input, and renderer owners through interception callbacks;
those subsystems are peers, not parts of the JIT. A renderer refactor is not a
CPU-migration prerequisite.

## Ownership table

| Subsystem | Responsibility | Current/target location | Entry point or boundary | Deep doc |
| --- | --- | --- | --- | --- |
| Product composition | Compose game-file resolution, host subsystems, native entry, and the one gameplay executor | `runtime/app/` | `main.c`, `port_entry.c` | `docs/migration.md` |
| Game-file identity and activation | Resolve installer/EXE/tree/ZIP inputs, validate exact LF2 identity and complete data, persist accepted root | `runtime/app/` | `game_data.c`, `game_selection.cpp`, `installer_extract.cpp` | `docs/running.md` |
| Typed configuration | Own defaults, validation, load/save, settings schema, and the only process-environment read boundary | `runtime/app/` | `config.c`, `options.c`, `environment.c`, `environment_keys.inc` | `AGENTS.md` |
| OS user-data placement | Compose LF2-specific filenames below Lucent's portable user-data root | `runtime/app/` and Lucent platform support | `user_paths.c` | `AGENTS.md` |
| Native startup | Bypass guest PE entry/WinMain and run verified bounded initialization before normal guest execution | `runtime/app/`, `runtime/overrides/` | `port_entry.c`, `startup_init.c`, `startup_frontend.c`, `startup_world.c`, `boot_guest.c` | `docs/re-frontier.md` |
| LF2 executor adapter | Bridge the established LF2 memory/PE/service owners to `x86port` CPU state, runtime calls, interception, exits, and invalidation | `runtime/cpu/` | One cohesive executor interface consumed by `port_entry.c` and overrides | `docs/migration.md` |
| x86 CPU and JIT | Decode, architectural state, flags, x87/SSE, exceptions, host emission, executable memory, and translated-block lifetime | Exact checkout resolved by `tools/build/source_dependencies.py` | `x86port` public product API | `docs/migration.md` |
| Bounded product fallback | Preserve architectural progress after failed/unsupported compilation or unsafe translated execution; report reason and coverage | Exact x86port checkout | Internal x86port product contract, never an LF2 engine selector | `docs/migration.md` |
| Test oracle | Independent diagnostic execution over the same CPU/memory contract | Exact x86port checkout plus LF2 test composition | Separately built test target | `docs/migration.md` |
| Guest memory and PE image | Map the authenticated PE and guest arenas; expose checked reads/writes and executable-write notification | `runtime/cpu/` | `guest.c`, `guest_map.h` and the LF2 executor adapter | `docs/migration.md` |
| Runtime interception | Classify import/HLE sentinels, native overrides, scoped original calls, return sentinels, and bounded exits | `runtime/cpu/`, `runtime/overrides/`, `runtime/win32/` | LF2 executor dispatch callback | `docs/migration.md` |
| Native override registry | Associate complete runtime guest identity/address with typed native behavior; provide scoped original-call policy | `runtime/overrides/` | `native_override.c`, `native_override.h`, plus the executor adapter | `docs/re-frontier.md` |
| Native game behavior | Own deliberate LF2 behavior replacements and enhancements by subject | `runtime/overrides/` | `assets.c`, `background.c`, `boot_guest.c`, `cheats.c`, `coop.c`, `hud.c`, `input.c`, `menu.c`, `objects.c`, `screens.c`, `text.c` | `docs/re-frontier.md` |
| Win32 and CRT HLE | Window/message, CRT/import, case-insensitive path, GDI, DirectShow, and socket services | `runtime/win32/` | `imports.c`, `win32.c`, `paths.c`, `gdi.c`, `dshow.c`, `wsock.c` | `docs/platform-boundary.md` |
| DirectDraw COM boundary | Guest-memory COM vtables and DirectDraw calls into the native video owner | `runtime/win32/`, `runtime/video/` | `com.c`, `ddraw.c` | `docs/platform-boundary.md` |
| Video presentation | Completed-frame lifecycle, classic/native renderer, texture ownership, lighting, stage geometry, and diagnostics | `runtime/video/`, `runtime/shaders/`, `stages/` | `host_frame.c`, `render.c`, `engine.c` | `docs/stage-geometry.md` |
| Audio | DirectSound guest surface, mixer, and platform music decode | `runtime/audio/` | `dsound.c`, `mixer.c`, `music_decode_*` | `docs/platform-boundary.md` |
| Input | Keyboard, gamepad, touch state, stable action bindings, and device ownership | `runtime/input/` | `bindings.c`, `keyboard.c`, `gamepad.c`, `touch_input.cpp` | `docs/running.md` |
| Host UI | RmlUi document/backends, setup dialog, device art, and touch presentation; edits configuration but does not own it | `runtime/ui/` | `settings_ui.cpp`, `setup_ui.c`, `touch_controls.c` | `AGENTS.md` |
| Platform integration | Android JNI/lifecycle and pre-window policy; desktop packaging metadata | `runtime/platform/`, `platforms/` | `android_bridge.c`, `window_policy.c` | `docs/running.md` |
| Process logging | Accept explicit typed LF2 records and delegate sink/filter/format policy to Lucent; product modules never write standard streams directly | `runtime/log/`, `third_party/lucent/` | `lf2_log.cpp` | `AGENTS.md` |
| Build and launch | Locked Python provisioning/build policy, immutable shared-runtime pins, and a slim zero-argument shell shim | `bootstrap.py`, `run.sh`, `tools/build/`, `CMakeLists.txt` | `run.sh` -> `bootstrap.py`; `tools/build/source_dependencies.py` | `docs/running.md` |
| Packaging | AppImage and Android assembly, inspection, signing interface, updater metadata, and asset-exclusion gates | `tools/build/`, `platforms/`, `CMakeLists.txt` | `appimage.py`, `android.py` | `docs/running.md` |
| Runtime route verification | State-anchored interactive scenarios, capture/analyzer helpers, and positive/negative reachability evidence | `tools/e2e.py`, `tools/routes/` | `tools/e2e.py` | `docs/migration.md` |
| Offline verification | Unit, format, lint, structure, asset, package, and pure production-seam checks | `tests/`, `tools/build/` | CTest and repository Python verifier | `AGENTS.md` |
| Asset-free CI | Build the Linux x86-64 native/JIT product and run focused boundary/quality tests from exact full-history inputs without `lf2.exe` or other game assets | `.github/workflows/ci.yml` | GitHub Actions -> `tools/build/build.py` and focused CTest | `docs/project-state.md` |
| Reverse engineering | Shared Python/Jython Ghidra automation, address/name metadata, traces, and binary-derived evidence | `tools/re/`, `re/`, `docs/info/`, `docs/re-frontier.md` | `tools/re/ghidra_scripts/DecompDump.py` and project information tools | `docs/isa-scope.md` |

## Where does new work go?

| Change | Owner |
| --- | --- |
| x86 decode, flags, instruction semantics, x87/SSE, emitter, or cache behavior | `shared/x86port` |
| LF2 CPU/memory conversion, interception, bounded runtime call, or executable-write notification | LF2 executor adapter under `runtime/cpu/` |
| A title address, identity check, native override, or scoped original-call registration | `runtime/overrides/` |
| Win32/CRT/GDI/COM behavior visible to any LF2 guest call | `runtime/win32/` or the existing host subsystem it forwards to |
| Drawing, textures, lighting, frame lifetime, or GPU diagnostics | `runtime/video/` |
| Persistent user option, default, or environment override | `runtime/app/config.*`, `runtime/app/options.*`, and typed keys in `runtime/app/environment*` |
| Settings presentation for an existing option | `runtime/ui/` |
| Cross-platform logging behavior | Lucent; LF2-specific explicit record adaptation stays in `runtime/log/` |
| Product launch/build/provisioning policy | Python owner in `bootstrap.py` or `tools/build/`; never `run.sh` |
| Shared x86port/jit-common revision, checkout validation, or provisioning | `tools/build/source_dependencies.py` |
| Repeatable gameplay scenario or runtime discriminator | `tools/routes/`, registered once in `tools/e2e.py` |
| Binary-derived fact or native replacement grounding | `docs/re-frontier.md`, a claim, or one issue according to the fact's consumer |

## Source tree

```text
lf2/
├── runtime/
│   ├── app/         native entry, lifecycle, selection, configuration, user paths
│   ├── cpu/         LF2 PE/memory owner and x86port integration boundary
│   ├── win32/       Win32/CRT/GDI/COM/HLE services
│   ├── overrides/   title-native behavior and runtime override registrations
│   ├── video/       renderer and completed-frame ownership
│   ├── audio/       DirectSound and native mixing/decode
│   ├── input/       keyboard, controllers, touch, action bindings
│   ├── ui/          RmlUi/setup/touch presentation
│   ├── platform/    platform runtime glue
│   ├── log/         LF2-to-Lucent adapter
│   └── shaders/     authoritative renderer shaders
├── platforms/       package/platform metadata and composition
├── stages/          optional authored native stage geometry
├── tests/           product seams and separately built test target (diagnostics included)
├── tools/           Python build, verification, packaging, and RE tooling
└── docs/            intent, state, migration, ownership, evidence, and RE facts
```
