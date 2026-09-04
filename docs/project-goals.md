# Project goals

This document owns durable LF2 product intent. Factual coverage is in
`docs/project-state.md`, execution order and acceptance gates are in
`docs/migration.md`, and atomic work is in `docs/issues/`.

## G001 — Ship one faithful native/dynarec LF2 product

Little Fighter 2 v2.0a runs from the player's authenticated original executable
with selected behavior owned natively and every remaining guest instruction
dynamically translated by `shared/x86port`.

Why it matters: the port must preserve the original game without shipping its
assets, depending on maintainer-only analysis tools, generating a title-sized C
corpus, or maintaining two gameplay engines.

Success conditions:

- A fresh checkout validates and consumes the player's exact LF2 v2.0a image
  directly; build, install, provisioning, and release paths emit no guest
  source, object corpus, or precompiled title substrate.
- The gameplay target links one execution path: native overrides plus the
  `x86port` JIT. An interpreter is available only in separately built test
  targets, including diagnostic tests. Build-graph, link-map/symbol, and
  selector inspection prove interpreter execution, interpreter-backed helpers,
  and fallback machinery are absent from gameplay.
- Runtime override lookup, disabled-override diagnosis, and scoped original
  calls are address- and image-aware and do not require regeneration.
- The existing native entry, Win32/DirectDraw/DirectSound/GDI HLE, guest-memory
  contract, and verified native overrides are preserved through the migration.
- The representative gameplay conformance gate in `docs/migration.md` passes
  on every released host architecture before the offline translator and its
  generated-only metadata are removed.

Constraints and non-goals:

- Do not rewrite portable game logic or hand-port the remaining game merely to
  avoid implementing JIT coverage.
- Do not use a renderer redesign, content-dependent image analysis, or a
  special case for one guest address as a CPU-correctness substitute.

Contributing state items: S001, S005, S015–S019.

## G002 — Preserve LF2 and deliver discoverable native enhancements

The product retains the original game's complete local experience while
providing modern presentation, input, configuration, and native host behavior
through cohesive owners.

Why it matters: changing the CPU execution owner is useful only if the game and
the port's user-visible improvements remain intact.

Success conditions:

- Boot, menus, character selection, VS Mode, Stage Mode, audio, timing, saves,
  and every retained original mode pass representative gameplay checks.
- Widescreen and ultrawide render additional world coverage through the
  existing deterministic view/camera/draw boundaries, never final-image
  stretching or frame-aware sampling.
- High-resolution pixel-art presentation, native lighting and cast shadows,
  anti-aliased host text, and window modes remain configurable in the in-game
  UI.
- Keyboard, controller, touch, two-player assignment, hot-plug, and persistent
  remapping are verified with representative physical devices.
- Original network play is either implemented faithfully through its native
  owner or remains plainly reported as missing; it is not hidden behind a
  generic compatibility claim.

Contributing state items: S001–S003, S006–S011, S014–S015.

## G003 — Deliver lawful, portable desktop and Android releases

Linux, macOS, and Android packages launch without a terminal, guide the player
to their own game files, store configuration in OS user data, and contain no
unlicensed LF2 content.

Why it matters: a source-only proof is not a usable port, and a package that
contains original assets is not distributable.

Success conditions:

- Zero-argument `./run.sh` is a slim locked-environment launcher for the live
  native/JIT product and works from a fresh clone with documented native
  dependencies and player-owned game files.
- AppImage, macOS, and Android first-run setup accept the exact executable,
  original installer, complete tree, or one bounded nested ZIP as supported by
  the platform, validate the full install, and preserve a previous valid choice
  on failure.
- Packages contain only redistributable port/runtime resources and no original
  executable, installer, extracted data, or reconstructable instruction bytes.
- x86-64 and ARM64 product JIT backends pass executable-memory, instruction-
  cache, ABI, correctness, performance, and sustained-device gates for the
  hosts on which they are released; no host falls back to interpretation.

Contributing state items: S004, S012–S013, S016–S019.
