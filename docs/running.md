# Running and verification contract

## Current execution state

The x86-64 `lf2` target links `x86port_runtime`. Its bounded silent run reached
native startup, completed native data initialization, loaded game art, and then
refused `MUL EDX` at guest `0x00401E27`, the first missing emitter. The pinned
runtime does not yet provide bounded fallback, so this is an implementation
frontier rather than playable-game evidence. ARM64 has no JIT backend yet.

The next differential target is defined in `docs/migration.md`. It executes
guest `0x004031b0` from the authenticated image through `shared/x86port` and
compares complete state and writes with the separately built test oracle.

## Focused boundary validation

From the repository root:

```sh
uv run --frozen python tests/test_execution_boundary.py
uv run --frozen python tests/test_source_policy.py
uv run --frozen python ../../shared/re-harness/tools/project_state.py --root .
uv run --frozen python ../../shared/re-harness/tools/codemap.py check .
git diff --check
```

The execution test requires the product JIT adapter, `x86port_runtime`, the
typed native-address registry, and no explicit gameplay interpreter selector.
The source-policy test scans all first-party source, documentation, and tooling;
it rejects retired execution interfaces, non-launcher shell automation, Java
tool scripts, product environment reads outside the typed owner, and product
diagnostics outside the LF2/Lucent logger. Its controlled negatives prove each
class can fail. None of these commands runs the game.

## Shipping launcher contract

`tools/build/source_dependencies.py` owns the immutable x86port revision
`e18bf6e8c7f2e7afc92f3c0b4575398bd1ecd8c7` and jit-common revision
`75ce92882aba7d80a39822604ab3a294f9c8944e`. It validates an explicitly
configured or shared checkout as clean and exact, or provisions both under
`build/deps`. CMake consumes only the resolved paths.

After S005 restores the supported product path, zero-argument `./run.sh` must:

1. enter the repository root and delegate to the locked Python bootstrap;
2. resolve redistributable dependencies without Ghidra, Wine, or a prefilled
   JIT cache;
3. resolve player-owned LF2 input by explicit argument, environment/`.env`, then
   documented drop-in location;
4. validate exact LF2 v2.0a identity and the complete data tree;
5. build the native/JIT gameplay target when required; and
6. launch with JIT as the default and no explicit interpreter selector.

`run.sh` is the sole shell script and remains a slim shim. Provisioning,
validation, build policy, cleanup, platform branching, and actionable errors
belong in modular Python. The launcher never runs tests.

Build output belongs under `build/`. Runtime logs, captures, and RE output use
one stable activity directory under ignored `scratch/`, never `/tmp`.

## Player-owned game input

No executable, installer, extracted LF2 data, or reconstructable instruction
bytes may be committed or packaged. Source launch supports a validated explicit
path, environment/`.env` path, and repository drop-in. Packaged first run uses
the platform-native picker and persists the validated selection in OS user
data.

Packaged setup may accept the exact executable or one bounded nested ZIP and may
decode the original installer where supported. It validates exactly one title
match and the complete install before committing a selection. Unsafe paths,
duplicate matches, corrupt archives, and entry/size-budget violations preserve
the previous valid selection.

## Verification layers

### Product-seam suite

The normal suite exercises production modules directly and includes formatting,
Clang-Tidy, structure limits, portable configuration, package content, and
positive/negative unit tests. It covers:

- LF2 memory/PE adaptation to canonical `x86port` state;
- import, native-override, scoped-original, return-sentinel, and invalidation
  dispatch;
- JIT-default product composition with no explicit interpreter selector;
- bounded fallback reason/coverage accounting once that shared contract exists;
- exact unsupported-instruction reports; and
- retained native renderer, audio, input, UI, configuration, and package tests.

Maintainer evidence uses Clang without rejecting GCC or AppleClang for users.
Touched C/C++ is checked with the tracked clang-format and clang-tidy policy.

### Separately built oracle target

Diagnostic-only explicit interpreter execution belongs in a separate target.
It initializes independent JIT and oracle state from the same authenticated
image, compares complete architectural state and writes, reports denominators,
and stops at the first divergence. A controlled negative must produce the
opposite answer before a clean result is trusted.

### Serialized gameplay scenarios

Runtime scenarios launch one product instance at a time and remain
observational. State-anchored inputs report every requested action and reached
screen. A timeout or missing producer is a failure. Physical controllers are
isolated explicitly; automation does not seize focus or play audible output.

The representative restoration scenario is the bounded Stage Mode fight in
`docs/migration.md`. Fallback entries are reported but do not prove gameplay or
performance for the affected region.

## Diagnostics and configuration

- Lucent is the one process logger. `runtime/log/` accepts explicit complete LF2
  records; it does not own timestamps, channels, sinks, or serialization.
- Every discriminator reports observed denominators and demonstrates a
  controlled opposite result.
- `runtime/app/config.*` owns persisted player settings;
  `runtime/app/environment.*` is the only product environment reader;
  `runtime/app/user_paths.*` owns the LF2 user-data location.
- Environment variables are maintainer/diagnostic overrides only and never
  select a CPU engine.

## Release qualification

A package is not release-qualified until it contains only redistributable
native/JIT code and resources, passes no-terminal first-run selection, defaults
to the JIT without an explicit interpreter mode, reports bounded fallback
coverage, and passes the representative gameplay gate on the released host.

Desktop results do not qualify Android. Android additionally requires signed
physical-device evidence for touch/controller switching, orientation,
audio/lifecycle correctness, loading, memory, sustained frame-time percentiles,
and thermal behavior.
