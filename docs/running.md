# Running and verification contract

## Current migration state

The current checkout still composes gameplay from offline-generated guest C.
That is not the project target and is no longer a supported run path. Until
state item S005 lands, do not invoke `./run.sh`, build or launch the gameplay
target, regenerate guest C, or use old route output as new product evidence.

The first permitted executable work is the separately built bounded JIT test
target in `docs/migration.md`, including its diagnostic modes. It executes guest
`0x004031b0` from the authenticated image through `shared/x86port` and compares
it with that test target's interpreter. It is not a mixed gameplay product.

## Documentation validation during the planning milestone

From the repository root:

```sh
python3 ../../shared/re-harness/tools/project_state.py --root .
python3 ../../shared/re-harness/tools/codemap.py check .
git diff --check
```

These commands inspect documentation/ownership metadata only. They do not build,
translate, generate, or run the game.

## Shipping launcher contract

After S005 restores the supported product path, zero-argument `./run.sh` must:

1. enter the repository root and delegate directly to the locked Python
   bootstrap through `uv run --frozen`;
2. resolve redistributable dependencies without requiring Ghidra, Wine, a
   generated guest corpus, or a pre-populated JIT cache;
3. resolve player-owned LF2 input in order: explicit argument, environment or
   `.env`, then the documented drop-in location;
4. validate exact LF2 v2.0a executable identity and the complete data tree;
5. build the native/JIT gameplay target when required; and
6. launch that product with no engine selector or interpreter fallback.

`run.sh` stays a slim shim. Provisioning, validation, build policy, platform
branching, and actionable dependency errors belong in Python. The launcher
never runs tests.

Build output belongs under top-level `build/`. Runtime logs, captures, and RE
output belong under one stable activity directory in gitignored `scratch/`,
never `/tmp`. A rerun overwrites or rotates one `.prev` result; it does not mint
numbered or dated trees.

## Player-owned game input

No executable, installer, extracted LF2 data, or reconstructable instruction
bytes may be committed or packaged. Source launch supports a validated explicit
path, environment/`.env` path, and repo drop-in. Packaged first run uses the
platform-native picker and persists the validated selection in OS user data.

Packaged setup may accept the exact executable or one bounded nested ZIP and may
decode the original installer where the existing title-specific decoder is
supported. It validates exactly one title match and the complete install before
committing a new selection. Unsafe paths, duplicate matches, corrupt archives,
and entry/size-budget violations preserve the previous valid selection.

## Verification targets after migration

Verification remains separate from the launcher and has three layers.

### Offline product-seam suite

The normal suite exercises production modules directly and includes formatting,
Clang-Tidy, structure limits, portable configuration, package content, and
positive/negative unit tests. It must include:

- LF2 memory/PE adaptation to the canonical `x86port` state;
- import, native-override, scoped-original, return-sentinel, and invalidation
  dispatch;
- gameplay link/symbol/selector exclusion of the interpreter, interpreter
  helpers, generated guest bodies, and engine choice;
- exact unsupported-instruction reports; and
- all existing native renderer, audio, input, UI, configuration, and package
  tests whose owner remains unchanged.

The project uses Clang for maintainer evidence without rejecting GCC or
AppleClang for users. Touched C/C++ is formatted with the tracked
`.clang-format` and linted with the tracked `.clang-tidy` against real compile
commands.

### Separately built JIT test target, including diagnostics

Only a separately built test target may link `x86port`'s interpreter; diagnostic
tests are included in that restriction. It initializes independent
JIT and interpreter state from the same authenticated image, compares complete
architectural state and writes, reports checked/skipped denominators, and stops
at the first divergence. Its controlled negative must produce the opposite
answer before a clean result is trusted.

No test option can cause the gameplay executable or library to gain an
interpreter, selector, or fallback.

### Serialized gameplay routes

Runtime routes launch one product instance at a time and remain observational,
not the general test gate. State-anchored inputs report every requested action
and reached screen. A timeout or missing producer is a failure, not an empty
success. Physical controllers are isolated explicitly; an automated run does
not seize focus or play audible output.

The representative retirement route is a bounded Stage Mode fight as defined
in `docs/migration.md`. Existing menu, character-select, VS, Stage, renderer,
audio, two-player, hot-plug, and UI routes may be adapted only after the JIT
product runs. Do not use the old generated-C product as their control arm.

## Diagnostics and configuration

- Lucent is the one process logger. `runtime/log/` assembles legacy fragments;
  it does not own timestamps, channels, sinks, or serialization.
- Every runtime discriminator reports the number of candidates/events/blocks
  it observed and demonstrates a controlled opposite result. Silence cannot
  mean success.
- Player settings are typed and persisted by `runtime/app/config.*` below the
  path supplied through `runtime/app/user_paths.*` and Lucent's OS user-data
  resolver.
- `runtime/ui/` edits settings but does not own them. Environment variables are
  maintainer/diagnostic overrides only and never hide a player feature or select
  a CPU engine.

## Release qualification

A package is not release-qualified until it contains only redistributable
native/JIT code and resources, passes no-terminal first-run selection, proves
interpreter/generated-code absence, and passes the representative gameplay gate
on the released host architecture.

Desktop results do not qualify Android. Android additionally requires signed
physical-device evidence for touch/controller switching, orientation,
audio/lifecycle correctness, loading, memory, sustained frame-time percentiles,
and thermal behavior. A debug emulator package is useful implementation
evidence but not a release gate.
