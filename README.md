# lf2-port

A **native Linux/macOS port of Little Fighter 2 v2.0a** by static recompilation — the x86
binary is translated to C, and the Windows APIs it calls are reimplemented on SDL3. Along
the way it gains the quality-of-life features the original cannot support: controller
auto-detect and hotswap, and borderless windowed mode.

> **Status: it plays the game.** The port boots, renders, navigates the menus, starts a
> VS-mode match, and has sound effects and background music.
> See [`docs/codemap.md`](docs/codemap.md) for an honest per-subsystem status, including
> what is still broken.

## No game content is distributed here

This repository contains **no Little Fighter 2 code, sprites, audio, or data** — only tools
and notes. Little Fighter 2 is freeware by **Marti Wong and Starsky Wong** and remains their
copyright. To use anything here you must download the official installer yourself from
<https://lf2.net> and extract it locally; the extracted tree is gitignored.

That applies to derivatives too. `re/instructions.tsv` — Ghidra's dump of every instruction
*with its raw bytes* — is gitignored for the same reason, since `.text` can be rebuilt from
it. Regenerate it from your own copy ([`docs/isa-scope.md`](docs/isa-scope.md)); the build
derives a substitute corpus when it is absent, so only the decoder-vs-Ghidra test skips.
What is kept is `re/entries.tsv` and `re/functions.tsv`: function addresses, sizes and
placeholder names, with no code in them.

This is an unofficial project with no affiliation with or endorsement by the LF2 authors.

## Building and running

```sh
curl -O https://lf2.net/LF2_v2.0a.exe
python3 tools/extract_game.py LF2_v2.0a.exe game/

cmake -S . -B scratch/build && cmake --build scratch/build -j
cd game && ../scratch/build/lf2 lf2.exe
```

Needs SDL3 and a C compiler. Extraction needs only Python 3 and its standard library — no
Windows, no Wine. Background music additionally needs `ffmpeg` on PATH at runtime (see
below); everything else works without it.

The working directory must be the extracted game tree, since the game opens its data by
relative path. Full details, including headless operation and the debugging environment
variables, are in [`docs/running.md`](docs/running.md).

## What works, and what doesn't

| | |
|---|---|
| Boot, menus, character select, a VS match | works |
| Rendering — DirectDraw, GDI text, colour-keyed sprites | works |
| Sound effects (DirectSound → SDL3) | works |
| Background music (WMA) | works, needs `ffmpeg` on PATH |
| Controller auto-detect and hotswap, no configuration | implemented, **untested on real hardware** |
| Two controllers | attaches and routes, but a second pad can land on a computer-controlled slot |
| Borderless / windowed / fullscreen, Alt+Enter | works |
| Linux | works |
| macOS | portability blockers removed, builds and passes under clang, **never built on a Mac** |
| Netplay | **not ported** — stubbed as "no network available" |

The untested rows are untested because no Mac and no gamepad were available, not because
they are known-broken. Reports welcome.

## Extracting the game files

The v2.0a installer is not Inno Setup or NSIS — it's a Win32 stub wrapping a custom `wwgT`
container. `tools/extract_game.py` reconstructs the full 690-file tree with no Windows
involved. Correctness is verified end to end: the game boots from the reconstructed tree.

The container format, including a deduplication trap that silently misaligns filenames
against their contents if you pair them naively, is documented in
[`docs/codemap.md`](docs/codemap.md).

## How it works

The recompiler decodes all 70,508 instructions of `lf2.exe` and emits one C function per
guest function — 100% of instructions lifted, no interpreter fallback. The runtime provides
a 4 GiB lazily-committed guest address space, lazy EFLAGS, x87 in host `double`, and
reimplementations of DirectDraw, DirectSound, DirectShow, GDI and the Win32 message loop on
SDL3. COM interfaces are synthesised as guest-memory vtables with sentinel addresses that
dispatch back into host C.

Correctness rests on differential testing against the host CPU: **8373 instruction
encodings × 8 rounds = 66,984 checks**, including x87, under **both gcc and clang**. Every
claim of the form "this is right" in the docs is expected to name the measurement behind
it, and several documented findings are corrections of earlier confidently-wrong ones.

Building under a second compiler is part of that, not housekeeping: it is what caught
`FSTP ST(i)` being emitted as `FST(i) = fpu_pop();`, which reads and modifies the FPU top
pointer unsequenced. Undefined behaviour that gcc happened to order correctly, so 66,984
passing checks said nothing about it.

### Notes from the reverse engineering

Full detail in [`docs/platform-boundary.md`](docs/platform-boundary.md) and
[`docs/isa-scope.md`](docs/isa-scope.md).

- **The porting surface is small.** An unpacked MSVC 2005 binary, 284 KB of code, ~130
  imported symbols, only 92 distinct mnemonics. `DirectDrawCreate` is the *only* DirectDraw
  import, so every other video call travels through COM vtables — which means the runtime
  gets to define them.
- **Part of every frame is drawn by GDI**, not DirectDraw. A naive "DirectDraw → texture"
  port silently loses the text rendering path.
- **Controller hotswap is impossible in the stock game by construction.** It enumerates
  joysticks once at startup, probes only device ids 0 and 1, and uses `joySetCapture` — a
  legacy API with no device-arrival notification. Replacing that surface is necessary but
  not sufficient: the game only consults a joystick for a player whose *control config*
  names one, so a correctly reimplemented `joyGetPosEx` can answer perfectly while a
  controller still does nothing. "Plug it in and play" needed the input gather ported.
- **Ghidra does not disassemble everything reachable**, so `re/instructions.tsv` is a lower
  bound. A live block using `FNSTCW` is absent from it entirely; "the binary contains no X"
  is not a conclusion that file can support.

## Repository layout

| Path | |
|---|---|
| `recompiler/` | x86 decoder and the x86 → C lifter |
| `runtime/` | guest machine, SDL3 backends, Win32/COM shims, test harnesses |
| `tools/` | installer unpacker, Ghidra scripts, analysis helpers |
| `docs/` | codemap, running guide, reverse-engineering notes |
| `re/` | Ghidra-derived function and instruction maps |
| `game/` | extracted game tree — **gitignored, supply your own** |

## Licence

[MIT](LICENSE).

This covers the tools, recompiler, runtime and documentation in this repository only. It
says nothing about Little Fighter 2 itself, which remains the copyright of Marti Wong and
Starsky Wong and is not distributed here.
