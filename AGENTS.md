# LF2 port — agent guidance

Little Fighter 2 v2.0a is becoming one native/dynarec product. Hand-written
native owners provide the host shell, Win32/DirectX services, renderer, audio,
input, UI, and selected game behavior. Every other guest instruction runs on
demand from the player's authenticated `lf2.exe` through `shared/x86port`'s
JIT, consumed by the x86-64 executor adapter through `x86port_runtime`.
Execution policy: the `dynarec-port`, `dynarec-runtime` and `dynarec-overrides`
skills.

## Authorities

Read `docs/project-state.md`, then `docs/migration.md`, before non-trivial work.

| Authority | Answers |
| --- | --- |
| `docs/project-goals.md` | durable product outcomes and success conditions |
| `docs/project-state.md` | verified, partial, blocked, and missing capabilities; current focus |
| `docs/migration.md` | the LF2 native/JIT migration: ownership boundary, first JIT discriminator (constructor `0x004031b0`), representative-gameplay restoration gate |
| `docs/codemap.md` | subsystem ownership and where new work belongs |
| `docs/re-frontier.md` | which native replacements are grounded in the binary |
| `docs/issues/` | open atomic work and unfixed defects |
| `docs/platform-boundary.md` | observed Win32, DirectDraw, DirectSound, GDI, and input surface |
| `docs/isa-scope.md` | title-specific x86 coverage evidence for the JIT |

## Ownership

- `x86port` owns x86 decode, architectural state, instruction semantics,
  dynamic translation, executable memory, and translated-block lifetime. LF2
  does not fork those responsibilities.
- LF2 owns exact executable identity, PE mapping, title callbacks, the native
  override table, Win32/HLE services, and product policy.
- Differential evidence comes from Wine/the original executable, binary
  analysis, or the separately built test oracle.

## Layout

The CPU migration adapts the existing product instead of rewriting it.

- `runtime/app/port_entry.c` is the native process entry. The guest PE entry
  and WinMain stay bypassed; bounded native initialization remains.
- `runtime/cpu/` holds the 4 GiB guest address space, PE loading, and memory
  access policy, adapted once to `x86port`'s CPU/memory interface.
- `runtime/win32/` is the Win32, CRT, GDI, DirectDraw COM, DirectShow,
  DirectSound, input, and socket/HLE service boundary.
- `runtime/overrides/` holds the verified native behavior and ABI facts, using
  address-based runtime dispatch and scoped original calls.
- `runtime/video/`, `runtime/audio/`, `runtime/input/`, `runtime/ui/`, and
  `runtime/platform/` are peer host subsystems. A renderer refactor is not a
  prerequisite for CPU execution; graphics is investigated only after
  CPU/service state proves a graphics-owned divergence.
- Existing routes are reusable evidence only after their producers run through
  the JIT product.
- `runtime/app/config.*` owns typed settings and persistence;
  `runtime/app/environment.*` owns the typed maintainer-override key set and
  is the only product source that reads the process environment;
  `runtime/app/user_paths.*` owns the LF2 path below Lucent's OS user-data
  directory; `runtime/ui/` edits configuration but does not own it. `LF2_*`
  environment variables are diagnostic/maintainer overrides, never features.
- Product modules log through LF2's typed Lucent logger bridge.
- `tools/build/source_dependencies.py` owns the immutable x86port and
  jit-common revisions, checkout validation, and clean provisioning under
  `build/deps`. CMake consumes only paths resolved by that owner.
- `tools/build/check_structure.py` caps new runtime source at 1,200 lines,
  freezes existing oversized files, and requires extraction before extending a
  2,000+ line file; it must cover new first-party modules.

## Game content

Source and packaged builds accept player-owned game files and validate the
exact LF2 v2.0a identity.
