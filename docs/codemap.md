# LF2 port — codemap

Static-recompilation port of Little Fighter 2 v2.0a (Marti Wong / Starsky Wong, freeware)
from 32-bit x86 Windows to native Linux/macOS.

Status legend: **done** (verified on real data) · **wip** · **planned** · **⛔ hack**

## Subsystems

| Subsystem | Where | Status | Notes |
|---|---|---|---|
| Installer unpacking | `tools/unpack_installer.py`, `tools/extract_game.py` | **done** | 690 files reconstructed; verified by booting the game from the output |
| Ghidra recon | `tools/ghidra/ListFunctions.java`, `re/functions.tsv` | **done** | 352 functions, 93.6% of `.text` |
| Wine oracle | `scratch/wineprefix` | **done** | boots headless under Xvfb to the main menu |
| Boundary tracer | `docs/platform-boundary.md` | **done** | Wine debug channels; relay tracing proven useless (see doc) |
| ISA scoping | `docs/isa-scope.md`, `re/instructions.tsv` | **done** | 70,508 instructions, only 92 mnemonics; top 50 cover 99.5% |
| Instruction differential | `runtime/test_insn.c` | **done** | 8373 encodings x 8 rounds = 66,984 checks vs the host CPU, incl. x87; negative-control validated |
| Recompiler: decoder | `recompiler/x86_decode.c` | **done** | length-exact on all 70,508 instructions; negative-control validated |
| Recompiler: lifter (x86 → C) | `recompiler/lift.c` | **done** | 74,135 / 74,136 lifted (100.00%); 1 TODO is decoded data, see below |
| Runtime | `runtime/guest.h`, `runtime/guest_ops.h` | **done** | CPU state, lazy flags, 4 GiB lazily-committed memory, PE load, import binding |
| Runtime (SDL3) | `runtime/ddraw.c`, `win32.c`, `gdi.c`, `gamepad.c` | **done** | video / input / Win32 shim; audio is silent (no WMA decoder) |
| Controllers | `runtime/gamepad.c` | **done** | SDL3 gamepad; auto-detect and hotswap (untested on hardware) |
| Input path | `runtime/win32.c` | **done** | keyboard and mouse verified into game state; menu navigates |
| Window modes | `runtime/win32.c` | **done** | windowed / borderless / fullscreen, Alt+Enter toggle |
| Netplay | `runtime/wsock.c` | **stubbed** | reports started-but-no-network, which the game handles |
| Startup crash | `docs/current-crash.md` | **fixed** | function-end detection; see doc |
| Rendering | `runtime/ddraw.c`, `runtime/gdi.c` | **done** | menus, screens and GDI text all render |
| Game flow | `docs/running.md` | **done** | reaches gameplay: a VS-mode match runs unattended, see the click/key script |
| Sprite colour-key | `runtime/ddraw.c` | **broken** | fighters draw in opaque black boxes; measured with `LF2_CK_DEBUG`, see below |

## The binary

`game/lf2.exe` — PE32 i386, MSVC 2005, **unpacked** (no packer), image base `0x400000`.

| Section | RVA | Size | |
|---|---|---|---|
| `.text` | `0x1000` | `0x4530a` (284 KB) | 352 functions |
| `.rdata` | `0x47000` | `0x56c0` | |
| `.data` | `0x4d000` | `0xc724` | |
| `.rsrc` | `0x5a000` | `0x2f42f0` (3 MB) | dominates file size |

Code is dominated by monoliths — largest functions are 28 KB, 20 KB, 18 KB, 15 KB
(`FUN_0041bc90`, `FUN_004246b0`, `FUN_00429730`, `FUN_0042e100`). These are the main loop
and character state machine. **Static recompilation exists specifically so we never have to
hand-port these.**

## Platform boundary (the entire porting surface)

Only ~130 imported symbols. This is what the runtime must implement:

| DLL | Imports | Replacement |
|---|---|---|
| `DDRAW` | `DirectDrawCreate` **only** | our own COM vtables → SDL3 GPU |
| `WINMM` | `joyGetNumDevs`, `joyGetDevCapsA`, `joyGetPosEx`, `joySetCapture`, `joySetThreshold`, `timeGetTime`, `mmio*` | SDL3 gamepad; **the legacy joystick API is why hotplug does not work today** |
| `DSOUND` | ordinal #1 (`DirectSoundCreate`) | our own COM vtables → SDL3 audio |
| `USER32` / `GDI32` | window, message pump, `StretchBlt` | SDL3 window; `StretchBlt` is the scaling path → borderless |
| `WSOCK32` | 19 ordinals | **stubbed**, netplay dropped |
| `WININET` | 4 | stub (online version check / ad banner) |
| `MSVCR80` / `MSVCP80` | 60 | host libc / C++ stdlib |
| `COMDLG32`, `SHELL32`, `ole32` | 4 | file dialog, URL open, COM init |

**Design consequence:** `DirectDrawCreate` is the only DDraw import, so every other video
call reaches the game through a COM vtable the recompiler cannot resolve statically. The
runtime supplies those vtables. Same for DirectSound.

## Data layer

The game is heavily data-driven — `game/data/data.txt` indexes 40+ `.dat` object files
(characters, weapons) plus 183 WAVs. The `.dat` files are **encrypted**; the decryption
routine lives in the exe and is not yet located. Character frame data lives in these files,
not in code, which is why the exe is as small as it is.

## Installer container format

The v2.0a installer is a Win32 stub with a custom overlay (not Inno/NSIS):

```
'wwgT' + id + u16
script records:  comp_size u32, uncomp_size u32, method u8, stream
                 (comp_size covers method byte + stream + 4-byte trailer)
                 record 5 is the file table
file payload:    method u8, stream   -- repeated back to back,
                                        NO per-file lengths, NO trailer
```

Streams are zlib (method 1) or bzip2 (method 2); boundaries are recovered from the
decompressors' `unused_data`. File-table entries are length-prefixed: comp size at `+10`,
uncomp size at `+18`, NUL-terminated path at `+62`.

**Trap:** the installer stores each distinct file once. 17 table entries are duplicates that
reuse an earlier blob (e.g. `bg/template/2/pic2.bmp` is byte-identical to `template/1`'s).
A naive sequential name↔blob pairing misaligns and silently writes wrong content.

## re/instructions.tsv is not a complete census

Ghidra does not disassemble everything reachable. The block at `0x4450ec` — a live CRT
check that reads the x87 control word — is absent from `re/instructions.tsv` entirely,
even though `fn_004450d0`'s declared size covers it. Anything scoped by grepping that file
is therefore a **lower bound**, not a total, and a "there are none in the binary" answer
obtained from it is worthless: the corpus excludes exactly the regions in question.

The lifter's control-flow end detection does not share this blind spot — it decodes from
the bytes — which is why it found the block. When the two disagree, the lifter is the
better instrument.

## Open: sprite colour-key

In gameplay the fighters draw inside opaque black rectangles. `LF2_CK_DEBUG=1` measures
both halves of the mechanism over a full match:

```
colour-key: SetColorKey=392 keyed blits=0 unkeyed blits=13083
SetColorKey #1 flags=00000008 key=002ffe48     (0x08 = DDCKEY_SRCBLT)
Blt flags=01000000 (has_key=1)                 (0x01000000 = DDBLT_WAIT)
Blt flags=01000800 (has_key=1)                 (+0x800 = DDBLT_DDFX)
```

The game sets source colour keys on surfaces that reach `Blt` with `has_key` set, and then
never passes `DDBLT_KEYSRC` (0x8000) on any of 13,083 blits. `BltFast`, the usual sprite
path, is never called. The argument mapping was checked against `IDirectDrawSurface::Blt`
rather than assumed, since a wrong index caused a real bug in this tree before.

**Established by experiment: the sprites arrive through `Blt`.** `LF2_CK_FORCE=1` honours
the key on every blit whose surface has one. With it the black boxes disappear entirely —
fighters composite cleanly and the sky shows its clouds and mountains. That rules out the
earlier hypothesis that the game composited sprites itself through `Lock`; the `Blt` path
is where the fault lives.

**`LF2_CK_FORCE` is a discriminator, not a fix.** The same screenshot shows the floor gone
transparent, so background surfaces carry colour keys too, and honouring every key trades
one artefact for another. Kept because it distinguishes the two failure classes in one run.

### Narrowed to one boolean

`DDBLT_KEYSRC` is not a constant the game forgets — it is computed, at `0x0043f14c`:

```
0043f14c  MOV EDX, EBP
0043f14e  NEG EDX            ; CF = (EDX != 0)
0043f150  SBB EDX, EDX       ; EDX = -CF
0043f152  AND EDX, 0x00008000  ; DDBLT_KEYSRC
0043f15a  OR  EDX, 0x01000000  ; DDBLT_WAIT
0043f160  PUSH EDX             ; dwFlags
```

The `NEG`/`SBB` pair is the standard carry-materialising idiom, so the key is requested
**iff `EBP != 0`**. The sibling branch at `0x0043f138` does the same with `0x01000800`.
Both branches are observed, always without `0x8000`, so **`EBP` is always 0 here** when it
should sometimes be non-zero.

`NEG` itself is not the bug: `f7 da` is in the instruction differential corpus and passes
all 8 rounds against the host CPU. The fault is upstream, in whatever computes the
colour-key boolean that reaches `EBP`.

So this is a **guest-code divergence, not a runtime gap** — the `Blt` handler is doing
exactly what it is told. The next step is to find what writes `EBP` before `0x0043f14c`
and compare that against the Wine oracle, rather than anything in `runtime/ddraw.c`.
