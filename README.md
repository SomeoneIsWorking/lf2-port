# lf2-port

Work toward a **native Linux/macOS port of Little Fighter 2 v2.0a** by static recompilation,
with the quality-of-life features the original can't support — controller auto-detect and
hotswap, borderless windowed mode, and native builds on non-Windows platforms.

> **Status: early. There is no playable port yet.**
> What exists today is reverse-engineering groundwork: an installer unpacker, a Ghidra
> function map, and a traced map of the game's platform boundary. The recompiler and runtime
> are not written. See [`docs/codemap.md`](docs/codemap.md) for an honest per-subsystem status.

## No game content is distributed here

This repository contains **no Little Fighter 2 code, sprites, audio, or data** — only tools
and notes. Little Fighter 2 is freeware by **Marti Wong and Starsky Wong** and remains their
copyright. To use anything here you must download the official installer yourself from
<https://lf2.net> and extract it locally; the extracted tree is gitignored.

This is an unofficial project with no affiliation with or endorsement by the LF2 authors.

## Extracting the game files

The v2.0a installer is not Inno Setup or NSIS — it's a Win32 stub wrapping a custom `wwgT`
container. `tools/extract_game.py` reconstructs the full 690-file tree on Linux or macOS
with no Windows and no Wine involved:

```sh
curl -O https://lf2.net/LF2_v2.0a.exe
python3 tools/extract_game.py LF2_v2.0a.exe game/
```

Only Python 3 and its standard library are required. Correctness is verified end to end: the
game boots from the reconstructed tree.

The container format, including a deduplication trap that silently misaligns filenames
against their contents if you pair them naively, is documented in
[`docs/codemap.md`](docs/codemap.md).

## What the reverse engineering found

Full detail in [`docs/platform-boundary.md`](docs/platform-boundary.md). The headline results:

- **The porting surface is small.** `lf2.exe` is an unpacked MSVC 2005 binary with 284 KB of
  code and only ~130 imported symbols. `DirectDrawCreate` is the *only* DirectDraw import,
  so all other video calls travel through COM vtables — which means the runtime gets to
  define them.
- **Part of every frame is drawn by GDI**, not DirectDraw (1651 `GetDC`/`ReleaseDC` pairs per
  20 s). A naive "DirectDraw → texture" port silently loses the text rendering path.
- **Controller hotswap is impossible in the stock game by construction.** It enumerates
  joysticks exactly once at startup, probes only device ids 0 and 1, and uses `joySetCapture`
  — a legacy API with no device-arrival notification of any kind. This isn't a bug to patch
  around; replacing that surface is the fix.

## Repository layout

| Path | |
|---|---|
| `tools/` | installer unpacker and Ghidra scripts |
| `docs/` | codemap and reverse-engineering notes |
| `re/` | Ghidra-derived function map |
| `game/` | extracted game tree — **gitignored, supply your own** |

## Licence

Not yet chosen; all rights reserved for now. This covers the tools and documentation in this
repository only — it says nothing about Little Fighter 2 itself, which belongs to its authors.
