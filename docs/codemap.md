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
| ISA scoping | `docs/isa-scope.md`, `re/instructions.tsv` (gitignored, regenerate) | **done** | 70,508 instructions, only 92 mnemonics; top 50 cover 99.5% |
| Instruction differential | `runtime/test_insn.c` | **done** | 8373 encodings x 8 rounds = 66,984 checks vs the host CPU, incl. x87; negative-control validated |
| Recompiler: decoder | `recompiler/x86_decode.c` | **done** | length-exact on all 70,508 instructions; negative-control validated |
| Recompiler: lifter (x86 → C) | `recompiler/lift.c` | **done** | 74,135 / 74,136 lifted (100.00%); 1 TODO is decoded data, see below |
| Runtime | `runtime/guest.h`, `runtime/guest_ops.h` | **done** | CPU state, lazy flags, 4 GiB lazily-committed memory, PE load, import binding; ~13% of a core in play |
| Runtime (SDL3) | `runtime/ddraw.c`, `win32.c`, `gdi.c`, `gamepad.c`, `dsound.c` | **done** | video / input / Win32 shim; effects via DirectSound, music via ffmpeg |
| Controllers | `runtime/gamepad.c`, `runtime/overrides/input.c` | **done** | SDL3 gamepad; auto-detect, hotswap, and the pad merged into the game's own player buttons by the ported input gather — no configuration, keyboard still live, a second pad joins as Player 2. Regression-tested pad-only by `tools/controller_test.sh` and `tools/controller_2p_test.sh` |
| Input path | `runtime/win32.c` | **done** | keyboard and mouse verified into game state; menu navigates |
| Window modes | `runtime/win32.c` | **done** | windowed / borderless / fullscreen, Alt+Enter toggle |
| Widescreen | `runtime/ddraw.c`, `runtime/win32.c`, `runtime/gdi.c`, `runtime/overrides/menu.c`, `runtime/overrides/background.c`, `tools/widescreen_test.sh`, `tools/background_test.sh`, `tools/resize_test.sh`, issues #13 #20 #23 #28 #29 #30 | **wip** | **The window decides**, live, on the frame after a resize — there is no switch, because an env var read once at startup is a developer's escape hatch rather than a feature (issue #20). The composition follows the window's **aspect**, not its pixel width: a 1920x1080 window gets `550*1920/1080 = 978` of world scaled up to fill it, and a window narrower in aspect than 794x550 clamps to 794 and letterboxes. The surfaces that follow are allocated **once** at `WIDE_MAX` with their **pitch fixed**, and a resize only moves `s->w` — `vram_alloc` has no free, so reallocating per resize event would exhaust the arena during one drag of an edge; `Lock` reports w/h/pitch fresh every call, so the game picks the change up next frame. `ctest widescreen` asserts the whole table including the two cases that must NOT widen. What it gives is a genuinely wider field of view, not a scaled picture: the compose surface widens and the game's own viewport-width words are set to match, so the camera and the layer loops draw MORE WORLD. Everything fixed at 794 is **centred** instead — front end, mode menu, character select, overlay (one offset on the backbuffer→primary copy) and the in-match HUD strip (offset during composition, with GDI text in the HUD band following it). Full-width colour fills and single-blit backdrops are carried across the viewport. The **background layers are not yet right**, and the mechanism is now fully read out of the game (claim C017): a layer has a SPAN (`width:`) and an optional LOOP (`loop:`), and `fn_0041a250` draws it at `off = -(camera*(span-794))/(stage_width-794)`, tiling by `loop` up to the span when there is one and drawing once when there is not. A span is authored so the layer covers the 794 screen at **every** camera position with no margin, so a **non-looping layer has no picture left over for a wider view at any camera** — every stage's sky is non-looping. The whole pass is the override `runtime/overrides/background.c`, and the widescreen change in it is one substitution: the game's literal 794 becomes the live view width, in the parallax and in the tiling bound. A **looping** layer is carried past 794 at its own `loop:` step — the stage's layout continued; a **non-looping** one is drawn once and pinned, because there is no more picture. The camera is clamped to `stage_width - view_width` in the same file (issue #28, resolved), so a wide view no longer scrolls past the wall a character can walk to. `ddraw.c`'s contiguity heuristic is **deleted** — it could not tell the two layer kinds apart and repeated Brokeback Clif's middle cliff across the band with hard seams. `ctest background` holds all of it: byte-identical to the recompiled body at 794x550, differing at 1600x550, with a skewed-parallax arm proving the identity check can fail. A resize no longer leaves the previous size's picture standing beside a centred screen: the centring offset is applied to the full-width copy to the primary, so it never writes the leftmost `offset` columns, and those are cleared when the geometry moves (issue #29, `ctest resize`, with the clear disabled as the negative). What is left is the black beside a non-looping layer in a very wide view (issue #23, open) — deliberately not filled, because filling it means inventing layout the stage does not have; it belongs to the renderer in issue #30. The camera is still clamped to `stage_width - 794` too, so a wide view scrolls past the stage's walls (issue #28, open). Both are the same literal 794 and land together; `LF2_BG_TABLE=all` + `tools/bg_table_check.py` (instrument I006) is what the fix reads its numbers from, checked 12/12 against the shipped `bg.dat` |
| Pause menu | `runtime/pause.c`, `runtime/overrides/screens.c`, issue #22 | **done** | Escape or Start during a match. Built on declining to call `fn_004246b0__orig`, which freezes the world with nothing to save or restore; the present lives inside that body, so it is issued separately, and the frozen frame is snapshotted because the menu draws straight onto the primary. **RESUME / DROP OUT / LEAVE MATCH / QUIT GAME**, on mouse, keyboard and pad. Escape is withheld from the game during a match so its own quit prompt does not open underneath. The rows are built per pause, because **DROP OUT is per player**: it appears only when the device that OPENED the menu is driving a slot this port put there (`coop_owns`), and it calls the same `coop_leave` that an unplugged pad does — so the menu records which device paused rather than guessing. **LEAVE MATCH** drives the game's own way out — F4, then the pre-fight overlay's own Exit item, with the selection placed and the GAME's attack button dispatching it — and lands on the character-select screen with the roster cleared. It is named for what it verifiably does: reaching the front-end menu from there is one further step that is **not** established (Escape at that screen does nothing, measured), and issue #22 stays open for it. Both unpause FIRST — pausing works by declining to call the game's update, so a transition driven while frozen would be delivered to a game that never runs another frame. The confirm is issued through a device that is actually driving a player, because inside the game proper a device with no slot has nowhere for its buttons to go |
| Drop-in coop | `runtime/overrides/coop.c`, `runtime/overrides/coop_debug.c`, `runtime/overrides/hud.c`, `tools/coop_dropin_test.sh`, `tools/coop_select_test.sh`, `tools/two_human_match_test.sh`, issues #15 #16 #17 #19 #21 | **done** | **Works end to end, and always on** — there is nothing to switch on, because a feature nobody can find is not a feature: a device pressing for the first time while a match is *already running* claims a free player slot, and **chooses its character in its own HUD panel** — the candidate's portrait and bars appear in the empty box along the top and flash there, left/right cycle the game's own roster of 23, attack (A) locks in — and from the lock-in the pad drives it. **The fighter is not on the stage until then**: it cannot walk, be hit, or be seen, and the match carries on around a player who is still deciding (issue #19, which is what it looked like when the choice was made by a blinking body standing in the fight). A pad that is **unplugged** takes its fighter back out again, but only one this port put there. **How one slot can have a panel and no fighter**, which is the RE the feature turned on: the HUD strip (`fn_0041ae60`) and the stage pass (`fn_0041a5a0`) and the world step (`fn_004064d0`) all read the SAME per-slot byte, `this+4+i`, so not building the fighter leaves no panel to choose in and building it puts a body on the stage. The two passes have to disagree, and the only place they can is *between* them — `fn_0041ae60` is overridden in `hud.c` to raise the byte for a slot that is still choosing, call the game's own panel drawing, and put it back down. The panel that appears is therefore the game's own, its portrait and bars read off the record `coop.c` built, not a picture this port painted; and the fighter enters the world at exactly one point, the lock-in. The flash is that panel being drawn on an eight-frame period, *not* an alpha fade: the port's blit path is a colour-keyed copy of 8-bit paletted sprites with no blend anywhere in it, so fading would mean inventing per-object blending in the porting layer rather than using the game's. Cycling **rebuilds** the record (gate off, the game's own reset, the new data block, the same position) rather than swapping its data pointer, because animation frame numbers do not carry across characters — and that rebuild is where the panel's portrait, name and bars come from. Its device's buttons are withheld from the record until lock-in, so nothing is left in it on the frame the fighter arrives. Two-sided regression tests: `coop_dropin` asserts the pad's input reaches the joined record, `two_human_match` that input in a player record becomes movement — split that way because a fighter joining mid-fight gets knocked about, so displacement alone could not discriminate. The RE under it: `this` is `0x00458b00`; `this+404` is **400** object pointers on a `0x420` stride; an object is in the world iff the **byte at `0x00458b04 + index`** is 1 — read by `fn_004064d0`, by the stage's draw-list collection at `0x0041a5d0` and by the HUD strip alike; `this+2004` is the object-data registry — `data.txt`'s `<object>` list in file order — with each block's **id at +1780 and type at +1784** (0 = character), the type located by requiring a match on all 65 entries; `fn_004061d0` is the record's `__thiscall` reset and `+0x368`/`+872` its data pointer. The four-player cap is gone — the count comes from the device-selector table's own size. Player slot `i` **is** object index `i` — `fn_00419a60__orig` walks the selector and pointer tables in lockstep over eight entries — while a computer's fighter is unbound by that (one sits at index 11), which is why the joined mask tracks the character-select roster rather than the object slots. Two humans in a **match** are covered too (`ctest two_human_match`) — a gap `controller_2p` never reached, since it stops at character selection. The joiner takes a slot the game's own roster considers empty, and the roster it cycles is the registry entries of type 0 less the template — 23 playable fighters; `LF2_COOP_CHAR` pins where the cycle STARTS so a test gets the same fighter every run, and is the only `LF2_COOP_*` name that is not purely a diagnostic. The joiner takes the **lowest slot with no fighter in it** — a second human is Player 2 — rather than the lowest whose device selector is zero, which put a joiner in slot 4 with P2's box empty beside it (issue #21) |
| Native overrides, split by subject | `runtime/overrides/` (`overrides.h`, `world.h`, `menu.c`, `screens.c`, `input.c`, `coop.c`, `coop_debug.c`, `text.c`, `assets.c`) | **done** | The hand-written replacements for recompiled functions, divided by what the code is **about** rather than by which address it replaces — one screen's behaviour is usually spread over several overrides, and `fn_0043f010` alone draws every screen. The line that earns its keep is `coop.c` (what the game does) against `coop_debug.c` (how this port knows it did it): the input gather used to be a page of device routing buried in 250 lines of `LF2_COOP_*` instruments, now `coop_debug_tick()`. `world.h` is the game's object/player model — every address with the evidence that located it. Verified by re-running the drop-in selection before and after the split: the coop log lines are identical |
| Scripted routes | `runtime/gamepad.c`, issue #18, claim C011, instrument I004 | **done** | `LF2_VIRTUAL_PAD` scripts take `button@<screen>[+n]` (`charselect`, `overlay`, `match`) as well as `button:<frame>`, firing off the game's own drawing so a press aimed at the match lands there however long the load took; `virtual_pad_report()` names the screens a route reached and says outright when a press never fired. A scripted run also **ignores physical controllers** — an attached pad binds gamepad slot 0, the front end is driven from slot 0 only, and a real Xbox pad plugged in for play silently stalled every route test at the front end (`input: 0 gathers`, no screen reached). That was very nearly written down as CPU contention |
| Netplay | `runtime/wsock.c` | **stubbed** | reports started-but-no-network, which the game handles |
| Startup crash | `docs/current-crash.md` | **fixed** | function-end detection; see doc |
| Rendering | `runtime/ddraw.c`, `runtime/gdi.c` | **done** | menus, screens, GDI text and colour-keyed sprites all render; GDI text is anti-aliased through a system font when `SDL3_ttf` is present |
| Advertising removed | `runtime/overrides/menu.c`, `runtime/overrides/text.c` | **done** | panel + strips (`fn_00423b00`, descriptor `0x0044d060`) and the corner update notice (`fn_0043f010`, MENU_CLIP7 at 725,5) with its click target; verified by rect scan, no blits left in either region |
| Game flow | `docs/running.md` | **done** | reaches gameplay deterministically every run, by pad and by mouse+keyboard, on a presented-frame input schedule |
| Menus: one input model | `runtime/overrides/menu.c`, `runtime/overrides/screens.c`, `tools/mouse_test.sh` | **done** | every screen from the launcher to the match takes mouse, keyboard and pad: launcher (screens 0/6/7), the post-load **mode menu**, **character selection** (hover moves the slot cursor, click joins and picks) and the **pre-fight overlay** (`0x0044d06c`, phase word `0x0044d070`). A mouse-only route reaches a match with no key and no pad |
| Game's own mouse cursor | `runtime/overrides/text.c` | **done** | declined in `fn_0043f010`; the host cursor is the only one. `LF2_CURSOR_ON=1` restores it |
| Audio: PCM integrity | `runtime/guest_map.h` | **done** | the surface arena used to overrun the sound arena and the game played bitmaps as audio; arenas are now declared in one place with build-time overlap checks and runtime bounds |
| Guest clock | `runtime/imports.c` (`guest_ns`), `runtime/ddraw.c` (`frame_pace`), issue #18, claim C014 | **done** | Guest time is exactly **presented frames × 33.33 ms** plus the sleeps the game took, and never reads the wall — so how much of the game's timeline has passed by frame *N* is a property of the game, not of how fast or busy the machine is. The same route gives `charselect@906 overlay@1746 match@1968` on an idle box and under fourteen busy loops alike; with the old wall-derived clock the loaded run reached **no screen at all**. Real time moved to the **present**, which holds each frame until the wall reaches its due time: the guest counts, the host paces. Three load-bearing details, each found by its own failure — a `Sleep` is credited as a **floor** (`ms + 1`, because `Sleep(n)` returns after *at least* n, and crediting exactly *n* parks the game's pacer on its own boundary where it neither works nor waits); sleeps are credited **during play** as well as on the load's fast path (the startup waits produce no frames), which costs nothing once frames flow because 33.33 ms clears the 33 ms threshold the game compares against; and the pacer **drops its anchor while loading**, or every frame after the load is due far in the future. `LF2_CLOCK_SITES` (instrument I005) is what named the loop this had to accommodate |
| Load time | `runtime/overrides/assets.c` (`fn_004148a0`), `runtime/imports.c`, issue #8 | **done** | **8.4-10.5 s -> 1.2 s** of active loading. Two causes, both measured: the frame-pacing `Sleep` between loader steps (skipped while loading, and the guest clock is credited so the wait ends instead of becoming a spin), and the game decrypting every data file **one byte at a time through `fscanf`/`fprintf`** — 2.5 M guest->host import calls per load, now a native loop proved byte-identical to the game's own on all 77 files. `LF2_LOAD_PROF=1` reports where the rest goes: 74% is drawing |
| Sprite colour-key | `recompiler/lift.c` | **fixed** | root cause was ADC/SBB dropping the carry; see below |
| Stage ground fill | `runtime/ddraw.c`, issue #9 | **fixed** | `DDBLTFX.dwFillColor` was read from offset 16 (`dwRotationAngle`), so every colour fill painted a leftover stack dword — the navy rectangles over stage 1-1's ground. It is at offset 80; verified on a stage 1-1 frame dump |

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
| `WINMM` | `joyGetNumDevs`, `joyGetDevCapsA`, `joyGetPosEx`, `joySetCapture`, `joySetThreshold`, `timeGetTime`, `mmio*` | SDL3 gamepad. Reimplementing these was necessary but **not sufficient**: they answered correctly while a controller still did nothing, because the game only consults a joystick for a player whose control config names one. That is fixed in the ported input gather, not here |
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

## Fixed: sprite colour-key — ADC/SBB dropped the carry

Fighters drew inside opaque black rectangles. The chain, each step measured:

1. `LF2_CK_DEBUG=1` over a match: 392 `SetColorKey` calls, **0 of 13,083 blits keyed**.
2. `LF2_CK_FORCE=1` (honour every key) removed the black boxes, proving sprites arrive
   through `Blt` and refuting the hypothesis that the game composited them via `Lock`. It
   also turned the floor transparent, so it was a discriminator, never a fix.
3. Reading the call site showed `DDBLT_KEYSRC` is *computed*, at `0x0043f14c`:
   `MOV EDX,EBP / NEG EDX / SBB EDX,EDX / AND EDX,0x8000` — the standard
   carry-materialising idiom, so the key is requested iff `EBP != 0`.
4. `NEG` was verified correct (`f7 da` is in the differential and passes), so the fault
   was upstream.
5. The generated C showed it: `SBB EBP,EBP` emitted as `_a - _b`. **`ALU_C`/`ALU_F` mapped
   ADC to `ADD` and SBB to `SUB`, discarding the carry** — everywhere in the binary, not
   just here. `SBB r,r` therefore always yielded 0, so `AND 0x8000` always yielded 0.

The fix adds `F_ADC`/`F_SBB` flag kinds (the carry-in shifts the boundary case: with a
borrow in, `SBB` sets CF when `a == b`, which `SUB` does not) and emits both with the
incoming carry folded into result and flags. Verified: `DDBLT_KEYSRC` now appears in the
flags (`0x01008000`) and keyed blits go from **0 to 11,290** of 13,099 — with 1,809 still
unkeyed, so it is selective rather than the blanket behaviour of `LF2_CK_FORCE`.

### Why the instruction differential missed it

`1b d2` (`SBB EDX,EDX`) **was** in the corpus and passed every round. The harness pinned
`eflags` to `0x202`, so the incoming carry was always 0 — and with CF=0, `SBB r,r` is 0
whether or not the borrow is honoured. The test exercised only the negative class, so it
could never have contradicted the bug.

`want.eflags` now varies CF per round. With the carry restored to the lifter the suite
passes; with it dropped again 43 cases fail. DF is deliberately left at 0: it is a
direction control rather than an arithmetic input, and the string cases assume forward.

## Memory: measured, and one bounded limitation

A full match sits at **555 MB RSS**, flat from ~30 s onward, with file descriptors steady
at 14. There is no leak during play: RSS and fd count were sampled at 10/30/50/70/90 s and
do not move after load.

Where it goes: 371 MB is the guest address space (touched pages) and 96 MB the host heap.
Of the guest side, **316 MB is DirectDraw surfaces** — 394 allocations. That is the price
of the 32-bit XRGB decision: the game's surfaces are 8-bit indexed on Windows, so this is
4x, and every surface is zeroed at creation, which makes every page resident. The decision
itself is sound (the game creates no palette and adapts to whatever `GetPixelFormat`
reports) but it is not free.

**`vram_alloc` is a bump allocator that never frees — and measurement says leave it that
way.** All 394 allocations happen during load and none during play. More to the point, the
game releases **2** surfaces in an entire session (`com_release_report`, which counts
`IUnknown::Release` per interface). There is nothing meaningful to reclaim.

Adding a free list would mean refcounting COM objects, and getting that subtly wrong gives
a use-after-free on a surface the game still draws from. Taking on that hazard to recover
two allocations out of 394 is the wrong trade, so it is deliberately not implemented.

*What would change the decision:* the release counter climbing. If a stage-change path ever
released and reallocated surfaces in bulk, `com releases: IDirectDrawSurface=…` would show
it, and the arena would then need a free path rather than a bigger base.

## Open: the game does not use its streaming sound buffer here

Comparing DirectSound call mix against the Wine oracle over the same 25 s window:

| call | oracle | port |
|---|---|---|
| `Lock` | 12371 | 5 |
| `GetCurrentPosition` | 8668 | **0** |
| `Unlock` | 4125 | 5 |
| `CreateSoundBuffer` | 41 | 5 |

Against real DirectSound the game streams: it polls `GetCurrentPosition`, then locks and
writes 3528 bytes into a looping 352800-byte (4 s) buffer, roughly 500 times a second. In
this port it fills five buffers once and never streams at all — it never calls
`GetCurrentPosition` even once.

Audio nonetheless works: effects fire during a match and music plays. So this is a path the
game takes on Windows and not here, rather than an outright failure, and what that path
carries is not yet established.

One real bug was found and fixed on the way: `GetCurrentPosition` returned the same value
for the play and write cursors. The write cursor must **lead** — Wine reports playpos
246960 against writepos 250488, a 3528-byte (40 ms) lead — and reporting them equal tells a
streaming caller there is no room to write. That fix changes nothing measurable *yet*,
precisely because the game never reaches the call, but the old behaviour would have stalled
the stream the moment it did.

### What that next step found

Comparing `CreateSoundBuffer` against the oracle exposed a much larger bug. The
`DSBUFFERDESC` fields were read at the wrong offsets — `dwBufferBytes` at +12 (actually
`dwReserved`) and `lpwfxFormat` at +16... at +20 (actually `guid3DAlgorithm`). The correct
layout is `dwSize`+0, `dwFlags`+4, `dwBufferBytes`+8, `dwReserved`+12, `lpwfxFormat`+16.

Every sound buffer therefore came out as the 4-byte fallback with the default format: all
116 buffers in a run reported *"4 bytes, 22050 Hz 1ch 8bit"*, where the oracle creates them
at 7006, 34156, 41096, 144384 and 352800 bytes across several rates. Each effect was
playing two samples.

Fixed, buffers come out at their real sizes and formats (8480 @ 21000 Hz 16-bit, 60416 @
22050, 3758 @ 11025, 34310 @ 38400, …), the game's `Lock`/`Unlock` count goes from 5 to 116,
`SetVolume` appears 93 times where it did not before, and the mix peak drops from a clipping
32768 to 25161 — real audio rather than clicks.

**Confirmed against ground truth, not just plausibility.** After the fix the port creates
buffers at 7006, 34156, 41096, 144384 and 379640 bytes — exactly the sizes Wine reports
creating against real DirectSound, in the same order. The `DDSURFACEDESC` offsets in
`ddraw.c` were audited at the same time and are correct (0/4/8/12/16/36/72/104/108), as is
the `WAVEFORMATEX` reader and `Lock`'s parameter mapping.

**This is why "audio works" was not a safe conclusion.** Non-zero plays, non-zero peak and a
busy mixer were all true the whole time. Two samples of a loud waveform satisfies every one
of those. Nothing short of comparing against a real DirectSound would have caught it.

*Still open:* the game does not enter its streaming path here (`GetCurrentPosition` remains
at zero calls).

## GDI text: proportional advance

The game imports no `CreateFont`, so it draws with the device context's default font —
proportional on Windows — and sizes its own layout to that. Blitting SDL's fixed-pitch 8x8
debug font straight out overran the layout: the main menu's copyright block rendered as

```
by Marti Wong, Starsky Won        <- clipped
1999-2008, all rights rese        <- clipped
http://www.LittleFighter.c        <- clipped
```

against Wine, which shows all three lines complete. Character selection lost the end of
`(Press Left/Right to change music)` the same way.

`TextOutA` now advances by each glyph's measured ink width plus one pixel of side bearing,
with a half-cell for blanks. The widths are measured from the rendered glyphs rather than
tabulated, so there is nothing to keep in step if the font changes. All three lines now
render in full and match the oracle's content.

It is an approximation of a proportional face, not that face — matching Windows' System
font exactly would need the font itself. The remaining difference is glyph shape, not
layout.

## Menu and ad structure, for porting them natively

Established by differential call-chain tracing (`LF2_BLT_STACK=<x>,<y>` walks the guest
stack at the blit landing on a given destination), not by guessing:

- **`fn_004246b0`** is the menu. 4689 lines of generated C, covering the main menu, control
  settings and recording info. It draws everything and hit-tests everything.
- It reaches the character art through **`fn_00423840`** and the ad panel through
  **`fn_00423b00`**. The two call chains share their entire outer frame and differ only in
  that middle hop, which is what identifies them.
- **Neither `fn_00423b00` nor `fn_004242e0` is "the ad function".** Both are shared drawing
  helpers taking a descriptor; stubbing the first aborts the game, stubbing the second
  garbles the character art. `fn_004242e0` does contain the "To advertise on LF2" link
  handling — it references `http://www.littlefighter.com/advertise` and ShellExecute's
  `open` verb — but that is one branch of a general helper.

The menu computes a **selection index from the mouse position** and dispatches on it:

```
ECX = mouse_x (0x4546f0);  if (ECX < 260 || ECX > 547) skip
ECX = mouse_y (0x453cdc)
if (274 <= ECX <= 300) EAX = 1
if (305 <= ECX <= 330) EAX = 2
if (336 <= ECX <= 361) EAX = 3
... then dispatch on EAX
```

So the whole menu is mouse-position-driven by construction, and the ad elements are drawn
inline in the same function as everything else. That is why there is no small function to
replace: **removing the ads and making selection controller-native both require porting
`fn_004246b0` itself**, which is the substantial piece of work here rather than a stub.

Ad destination rectangles, for reference when porting: top strip `(0,0)-(397,34)`, right
panel `(590,199)-(788,393)`, its arrows `(750,185)-(769,199)` and `(769,185)-(788,199)`,
bottom strip around `(0,508)-(71,517)`. The character art is `(0,0)-(330,546)` and the
background `(0,0)-(794,550)`.
