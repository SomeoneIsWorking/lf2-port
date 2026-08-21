# LF2 port — codemap

Static-recompilation port of Little Fighter 2 v2.0a (Marti Wong / Starsky Wong, freeware)
from 32-bit x86 Windows to native Linux/macOS.

Status legend: **done** (verified on real data) · **wip** · **planned** · **⛔ hack**

## Subsystems

| Subsystem | Where | Status | Notes |
|---|---|---|---|
| Installer unpacking | `tools/unpack_installer.py`, `tools/extract_game.py` | **done** | 690 files reconstructed; verified by booting the game from the output |
| Ghidra recon | `tools/re/ghidra/ListFunctions.java`, `re/functions.tsv` | **done** | 352 functions, 93.6% of `.text` |
| Wine oracle | `scratch/wineprefix` | **done** | boots headless under Xvfb to the main menu |
| Boundary tracer | `docs/platform-boundary.md` | **done** | Wine debug channels; relay tracing proven useless (see doc) |
| ISA scoping | `docs/isa-scope.md`, `re/instructions.tsv` (gitignored, regenerate) | **done** | 70,508 instructions, only 92 mnemonics; top 50 cover 99.5% |
| Verification: the suite | `CMakeLists.txt`, `runtime/overrides/geom.h`, `tests/test_geom.c`, `runtime/video/framelife.h`, `tests/test_framelife.c`, `runtime/app/framespec.h`, `tests/test_framespec.c`, `tools/e2e.py` | **done** | **One ctest suite, about 15 s including Clang formatting and lint, and nothing in it boots the game.** That is a bar, not a description: a suite with a five-minute test in it stops being run, which is how the mouse route stayed green and broken for as long as it existed (issue #26). What CAN be checked offline is — `geom.h` holds the port's pure geometry (composition width, parallax, camera bounds and the wide-view centring, the pre-fight overlay's rows, the stereo pan) and `test_geom.c` walks all of it in a millisecond. The overrides **include** that header rather than keeping their own copy, so the test is exercising the code that ships, and `tools/e2e.py background`'s byte-identity arm confirms the factoring changed no pixel. The **renderer's frame lifetime** followed the same route (`framelife.h` + `test_framelife.c`): when a display list is cleared, how a frame held up under the pause menu is rewound, which pooled texture serves a tile — all bookkeeping over plain integers, and every bug in the pause menu's first cut was in it, each one found by looking at a 1080p screenshot after a five-minute route run (issue #53). The **frame-spec grammar** went the same way (`framespec.h` + `test_framespec.c`, 26 checks): `LF2_FRAME_DUMP` now takes `@screen+N` as well as a frame number, and a bug in that parser neither crashes nor prints — the frame is simply not dumped and the route reports "the run never reached it", which reads as a problem with the game. Its negatives were **vacuous on the first cut** and the test says so: a mutant of the header that ignored the unresolved flag passed all 26 checks, because the stub returned −1 for an unresolved anchor and −1 is never a frame anyone asks about. The stub now returns a plausible frame there, and the mutant fails — run against both classes before the test was believed. The audio pan is the worked example: a three-run, 270-second script became 20 assertions, and gained a walk across every on-screen pixel at 794 and 1920 that the script never did. The scripts that DO boot the game live behind `tools/e2e.py` (all of them, or by name), one at a time because each wraps its instance in a wall-clock `timeout`; they answer only what needs a running game — does a route reach a screen, does a second pad drive its fighter, does the GPU renderer match the software one |
| Instruction differential | `tests/test_insn.c` | **done** | 8373 encodings x 8 rounds = 66,984 checks vs the host CPU, incl. x87; negative-control validated |
| Recompiler: decoder | `recompiler/x86_decode.c` | **done** | length-exact on all 70,508 instructions; negative-control validated |
| Recompiler: lifter (x86 → C) | `recompiler/lift.c` | **done** | 74,135 / 74,136 lifted (100.00%); 1 TODO is decoded data, see below |
| Runtime | `runtime/cpu/` | **done** | CPU state, lazy flags, 4 GiB lazily-committed memory, PE load, import binding; ~13% of a core in play |
| Project structure | `AGENTS.md`, `CLAUDE.md`, `tools/build/check_structure.py`, global `game-port-structure` skill | **done** | Host ownership follows Dusklight's pattern rather than its platform implementation: `app` composes lifecycle/startup; `ui` separates the RmlUi document, device-independent input translation, and SDL backend; `input` owns device state and mappings; `video` owns rendering; and `overrides` owns guest behavior. The normal ctest suite caps new runtime sources at 1,200 lines, freezes files already above that at their current count, and reports 2,000+ lines as critical extraction territory. |
| Direct startup | `runtime/app/startup.c`, `runtime/overrides/boot_guest.c`, `runtime/video/ddraw.c`, issue #71 | **done** | The world constructor is overridden at `fn_00419e40`: the complete original constructor runs, then its initial top-level state is set to the local loader before the first update can draw or accept input. This does not replay the launcher's Game Start branch and does not manufacture a key, click, pointer, or controller event. The real loader runs unchanged, but its frames are discarded while the SDL window is hidden; the window is revealed only after the first mode-menu frame has been rendered and presented. The first script-visible screen is `modemenu` at frame 4. The retired launcher, replay entry, and legacy network menu are absent from the boot route; `wsock.c` is only a loud binary-compatibility stub, not the port's future multiplayer design. |
| Global RmlUi, input mapping and device art | `runtime/ui/settings_ui.cpp`, `runtime/ui/rmlui_input.cpp`, `runtime/ui/rmlui_backend.cpp`, `runtime/ui/device_assets.c`, `runtime/ui/device_icons.c`, `runtime/input/bindings.c`, `runtime/input/keyboard.c`, `runtime/input/gamepad.c`, `runtime/app/pause.c`, `runtime/app/config.c`, `tests/test_bindings.c`, `tests/test_device_icons.c`, `tools/routes/settings_test.py`, `tools/routes/ui_escape_test.py`, `tools/routes/ui_global_test.sh`, issues #70 #74 #77 #79 #80 #82 | **done** | Escape or Start opens one Dusklight-structured RmlUi shell directly on mode menu, character selection, overlay, and match. `rmlui_input.cpp` is the Dusklight-style device-independent boundary: configured keyboard and every attached controller feed Up/Down/Left/Right/Confirm/Cancel with latched edges and directional repeat. The physical X11/XTEST route proves Escape, mapped keyboard Confirm, and a real mouse click; the settings route proves mapped controller focus/Confirm reaches Controls. Pointer events use SDL's renderer transform once—not two density multiplies—and the context uses drawable dimensions plus content scale, so its outline font is 32px on the verified 2x display. While active RmlUi consumes all physical input before Win32 messages and zeros the guest gather. Shared `port-assets` SVGs are embedded rather than copied; native icons use the same linear host-coverage sampling as fonts and RmlUi uses a 120x120 linear-filtered raster. HUD indicators are suppressed while the pre-fight overlay borrows the HUD panel signal, so they cannot cover Stage/Difficulty controls. |
| Renderer: engine and stable composition | `runtime/video/engine.c`, `runtime/video/engine_textures.c`, `runtime/video/texture_lru.h`, `runtime/video/engine.h`, `runtime/video/render.c`, `tests/test_texture_lru.c`, `tools/routes/render_test.sh`, `tools/routes/texture_cache_test.py`, issues #64 #76 #83 | **done** | One SDL_GPU device, painter-order D32 depth, three blend pipelines, and a frame-safe texture cache. The display list is unchanged; the engine replaces what draws it. The whole world is composed on the native integer grid and scaled once as a finished texture, giving every scrolling layer one stable nearest-sampling phase. Guest sprites remain nearest; output-resolution host font/SVG coverage is linear. The cache protects every texture referenced by the current frame and LRU-reuses older entries only after a replacement uploads successfully. The Stage route filled all 512 entries, made 168 evictions at 123 peak live/frame, and lost no art. The software renderer remains the byte-identity A/B control. |
| Renderer: character shading and cast shadows only | `runtime/video/hd2d.c`, `runtime/video/engine.c`, `runtime/shaders/hd2d_character.frag`, `runtime/shaders/hd2d_shadow.frag`, `runtime/shaders/hd2d_light.frag`, issues #63 #75 | **done** | One key light shades only sprites the game's shadow-ellipse/object pairing identifies as characters. The same vector projects each character silhouette into a crisp cast-shadow mask. Outside those silhouettes only the shadow mask may modify the picture. Backgrounds, stage geometry, HUD, text, and bands retain authored colour. DoF, G-buffer distance, bloom, floor tint, backdrop relighting, haze, vignette, colour grade, their runtime options, and their diagnostics are deleted. |
| Scripted routes | `runtime/app/script.c`, `tools/routes/`, issue #18 | **done** | All devices use `@modemenu`, `@charselect`, `@overlay`, and `@match` anchors derived from game drawing. There is no `@frontend` anchor because startup never presents that screen and no route injects its boot input. Every configured stream reports fired/total and names every missed item. Physical controllers remain ignored in scripted runs so they cannot steal a slot. |
| Renderer: stage geometry | `runtime/video/mesh.c`, `runtime/video/mesh.h`, `runtime/video/stagegeom.c`, `runtime/video/stagegeom.h`, `tests/test_stagegeom.c`, `runtime/shaders/mesh.vert`, `runtime/shaders/mesh.frag`, `docs/stage-geometry.md`, `stages/`, `tools/routes/stage_geom_test.sh`, `tools/routes/mesh_test.sh`, `tools/re/stage_gaps.py`, issues #49 #62 | **done (dormant)** | **Optional hand-authored 3D sets.** `stages/<name>.stage` is keyed on the stage's own `bg.dat` name and names Wavefront OBJ models; `docs/stage-geometry.md` is the format. **`depth` is a property of the SOLID, not the vertex** — a pillar stands at one parallax depth while its vertices differ in x, jump and row — which is what lets a three-axis OBJ carry four-axis geometry. `depth: layer hill1.bmp` takes the depth from the stage's *own* layer (C031) so a solid cannot drift from the art it belongs with; a depth that cannot be derived is REFUSED, never defaulted. **The projection is the GAME'S, derived not chosen, and it is not a matrix**: a layer at depth *d* shifts by `camera/d` (C031), a fighter's z is used *directly* as a screen row (C018), but every object shifts by the camera **flat** whatever its z — two cameras glued together, which no perspective camera reproduces. `X - camera/d` is not linear in `(X, d, 1)`, so a 4x4 cannot express it either. Hence four independent per-vertex numbers, and the divide per vertex. `geom_stage_clip` is the copy under test; `mesh.vert` is a transcription and says so. **The record's own strings resolve the keys** (C033): `fn_0040c160`, the bg.dat parser, writes the stage's `name:` and each layer's bitmap path into the background record, so the port names both from the game's memory — no second decrypt, no `data.txt`, no load-order assumption. **`runtime/overrides/background.c` is where a set is placed**, because that override already holds the stage record, the layer spans, the view width and the draw-time camera. Placement is once per occupied gap in the layer order: a solid is drawn immediately before the first layer whose derived depth is `<= d`, an underivable layer depth reading as *infinitely far*. On the engine path each piece gets the **sliver** of depth between two list positions — ordered against the game's layers by the list, against other geometry in the same sliver by its own depth, which is exactly the pair that can interpenetrate. `mesh.c` is the standalone pass the SDL_Render path still needs, since it cannot take geometry at all; it keeps a self-test a broken depth test cannot survive (near triangle submitted first, far one over it, plus a far-only pixel to rule out a pass that drew nothing). Most of `ctest stagegeom` asserts the loader **refuses** invalid input, because every failure mode of a data loader here is silent. The rejected low-poly PvE prop pack was removed in `02f5be1`; `stages/` intentionally ships no scenes or models, and no stage geometry is active in the intended product. The guarded `stage_geom` route verifies the dormant integration and its no-geometry control. |
| Runtime (SDL3) | `runtime/video/ddraw.c`, `runtime/win32/`, `runtime/input/gamepad.c`, `runtime/audio/` | **done** | video / input / Win32 shim; effects via DirectSound, music via ffmpeg. The *software* game-composition path is not a GPU pipeline (the native renderer above is) — the frame is composed on the CPU by the software blitter and presented as one streaming texture. The stock game's indexed blits still have only copy and colour-key skip; port-owned SVG and text overlays alpha-composite through their separate UI boundary. This row used to say “SDL3 GPU”, which read as though geometry reached the GPU; it does not, and issue #30 is where that changes **Widescreen is CENTRED and no longer silences the right** (issue #39): the game centres the players' centroid in a 794-wide window (`SUB ESI,0x18d` at `0x0041bb7d`) and pans sound between two screen speakers at x 200/600 reaching 400 px — both are the 794 screen written down as pixels. The world is now drawn from a camera shifted left by half the extra width (a *draw-time* value: writing it back feeds `fn_0041b5d0`'s 1/7 easing and drifts to `target - 7K`), and the pan constants are scaled by `view/794`. `ctest geometry` walks every on-screen x at 794 and 1920 and has the unscaled constants as the negative arm **The port drives itself into any of the game's eight modes** (`LF2_MODE=<name>`, `runtime/overrides/menu.c`): it writes the game's own mode-menu selection and lets the route's confirm dispatch it. Before this every scripted route took whatever the menu was sitting on — VS — so seven of eight modes were untested and the stage-mode camera lock could not be verified at all. `tools/e2e.py stage_mode` uses it, with the VS run as the negative that proves the lock is a stage-mode signal **The mouse drives the game into a match on its own** (`tools/e2e.py mouse`: charselect → overlay → match, no key, no pad). Two port bugs were in the way and neither was visible in a screenshot: the pre-fight overlay's rows were a uniform 24-px step measured off three sampled highlight blits, where Ghidra on `FUN_00429730` gives them as 16/39/64/87/111/137 on a slant (claim C022 — the three rows sampled are exactly the three a uniform step gets nearly right); and an idle pointer counted as a *move* on the frame a screen opened, because each handler's last-position memory belonged to the handler and not the screen, so the overlay opened and immediately selected whatever the resting pointer was over. `tools/re/ghidra_scripts/DecompDump.py` is how a function gets decompiled now — see docs/running.md |
| Controllers | `runtime/input/gamepad.c`, `runtime/input/bindings.c`, `runtime/overrides/input.c` | **done** | SDL3 gamepad auto-detect and hotswap; persistent action mapping; the pad is merged into the game's own player buttons by the ported input gather, keyboard stays live, and a second pad joins as Player 2. Regression-tested pad-only by `tools/routes/controller_test.sh` and `tools/routes/controller_2p_test.sh` |
| Input path | `runtime/win32/win32.c`, `runtime/input/keyboard.c/.h`, `runtime/input/bindings.c/.h`, `runtime/input/gamepad.c/.h`, `runtime/overrides/input.c`, `runtime/ui/rmlui_input.cpp`, issue #81 | **done** | Keyboard, mouse and controller enter game state through their native paths; RmlUi edits the same action maps consumed by those shipping paths. The Win32 pump records physical keyboard state before modal consumption and latches Escape for the app shell. The UI translator consumes those mappings from keyboard and all four controller slots through the narrow gamepad interface; raw unbound keyboard controls retain standard RmlUi navigation. The settings route attaches all four virtual pads and drives the document only from slot four. While RmlUi is open it consumes SDL input and zeros the guest keyboard/pad gather. |
| Window modes | `runtime/win32/win32.c` | **done** | windowed / borderless / fullscreen, Alt+Enter toggle |
| Widescreen: the composition | `runtime/video/ddraw.c`, `runtime/win32/win32.c`, `tools/routes/widescreen_test.sh`, issues #13 #20 | **done** | **The window decides**, live, on the frame after a resize — there is no switch, because an env var read once at startup is a developer's escape hatch rather than a feature (issue #20). The composition follows the window's **aspect**, not its pixel width: a 1920x1080 window gets `550*1920/1080 = 978` of world scaled up to fill it, and a window narrower in aspect than 794x550 clamps to 794 and letterboxes. What it gives is a genuinely wider field of view, not a scaled picture: the compose surface widens and the game's own viewport-width words are set to match, so the camera and the layer loops draw MORE WORLD. The surfaces that follow are allocated **once** at `WIDE_MAX` with their **pitch fixed**, and a resize only moves `s->w` — `vram_alloc` has no free, so reallocating per resize event would exhaust the arena during one drag of an edge; `Lock` reports w/h/pitch fresh every call, so the game picks the change up next frame. `tools/e2e.py widescreen` asserts the whole table including the two cases that must NOT widen. |
| Widescreen: framing each screen | `runtime/video/ddraw.c`, `runtime/win32/gdi.c`, `runtime/overrides/menu.c`, `tools/routes/resize_test.sh`, issues #29 #42 #44 #51 | **done** | Everything the game fixed at 794 is **framed per screen** (issue #44): the front end and the mode menu are LEFT-aligned because their character portrait is drawn at a hard literal x=0 and hangs on the screen's left edge; the loading screen is centred with its side bands extended from its own edge columns (its backdrop is a picture, so that extension is a declared port choice and the run says so); character selection, the overlay and the in-match HUD strip stay **centred**. A screen is recognised by the full-screen fill colour it paints — 0x10206c, 0x122565, 0x000000, the first two appearing exactly once each in the whole binary — rather than by a `.data` word, because the word previously taken for the mode menu's cursor is the game MODE and reads 1/4/5 in a match (issue #51). The centring is applied **while composing**, to the draws that fit inside the game's own 794-wide screen, *not* to the backbuffer→primary copy, and that is what lets a screen's own full-screen colour fill act as the **background**: it spans the composition from the left edge with the artwork centred on top (issue #42 — a very wide short window used to jam the front end against the right with black down the left). Only a flat colour is extended; a backdrop that is artwork is not, because stretching a picture invents layout. Which fills are the STAGE's full-width bands is **marked by the background override** (`world_band_hint`, `LF2_BAND_DEBUG`) rather than guessed from the rectangle — the game's one fill helper `fn_00415160` serves both the stage's tinted layers and the front end's backdrop, so `0..794` matched the menu exactly. A 1:1 copy writes every column of the primary every frame, removing issue #29's ghost by construction; the centring offset is applied to that copy so it never writes the leftmost `offset` columns, and those are cleared when the geometry moves (`tools/e2e.py resize`, with the clear disabled as the negative). |
| Widescreen: the background layers | `runtime/overrides/background.c`, `runtime/video/ddraw.c`, `tools/routes/background_test.sh`, `tools/re/bg_table_check.py`, issues #23 #28 #30 #62 #66 | **done** | **The mechanism is fully read out of the game** (claim C017): a layer has a SPAN (`width:`) and an optional LOOP (`loop:`), and `fn_0041a250` draws it at `off = -(camera*(span-794))/(stage_width-794)`, tiling by `loop` up to the span when there is one and drawing once when there is not. A span is authored so the layer covers the 794 screen at **every** camera position with no margin, so a **non-looping layer has no picture left over for a wider view at any camera** — and every stage's sky is non-looping. The whole pass is the override `runtime/overrides/background.c`, and the widescreen change in it is one substitution: the game's literal 794 becomes the live view width, in the parallax and in the tiling bound. A **looping** layer is carried past 794 at its own `loop:` step — the stage's layout continued; a **non-looping** one is drawn once and pinned, because there is no more picture. The camera is clamped to `stage_width - view_width` in the same file (issue #28), so a wide view no longer scrolls past the wall a character can walk to. `ddraw.c`'s contiguity heuristic is **deleted** — it could not tell the two layer kinds apart and repeated Brokeback Clif's middle cliff across the band with hard seams. `tools/e2e.py background` holds all of it: byte-identical to the recompiled body at 794x550, differing at 1600x550, with a skewed-parallax arm proving the identity check can fail. For a wide composition, only layer zero is semantically marked as the painted backdrop and extended to the viewport; applying the rule to every short non-looping layer would wrongly stretch CUHK's grass patch and Queen's Island's lamp post. No replacement art or geometry is added. Nine real PvE backgrounds were independently selected and captured at 1920x1080 after match initialization with no uncovered stage columns or composition wedges (issue #66). `LF2_BG_TABLE=all` + `tools/re/bg_table_check.py` (instrument I006) reads the authored numbers and is checked 12/12 against the shipped `bg.dat`. |
| HiDPI | `runtime/win32/win32.c`, `runtime/video/ddraw.c`, `runtime/ui/settings_ui.cpp`, `runtime/ui/rmlui_input.cpp`, `tools/routes/hidpi_test.py`, issues #56 #82 | **done** | **Verified on a simulated 4K panel.** Game geometry is seeded from `SDL_GetWindowSizeInPixels`, and game-pointer hit tests map points through measured density. RmlUi separately sizes its context to the drawable, sizes `dp` and FreeType through `SDL_GetWindowDisplayScale`, and maps mouse events once with `SDL_ConvertEventToRenderCoordinates`; the removed path scaled motion before calling an adapter that scaled it again. In the 4K/200% nested KWin run the port reports `794x550 points -> 1588x1100 pixels`, retains 794 world columns at scale 2.000, and RmlUi reports content scale 2.00 with a 32px body font. `tools/e2e.py hidpi` refuses an unscaled output; `ctest geometry` walks the game pointer invariant across five densities. |
| Match modal behavior | `runtime/app/pause.c`, `runtime/overrides/screens.c`, issue #22 | **done** | The global RmlUi shell freezes a running match by declining `fn_004246b0__orig`; non-match screens continue behind it. The native renderer rewinds its retained display list before compositing RmlUi, so the modal cannot erase the frozen frame. **DROP OUT** appears only when the opening device owns a port-added fighter and calls the same `coop_leave` as disconnect. **LEAVE MATCH** closes the modal first, then invokes the game's native match-end and overlay-exit code; no F4 or attack is synthesized. `pause_dropout` verifies the retained renderer and per-device drop, and `exit_to_menu` verifies the native transition. |
| Object pass | `runtime/overrides/objects.c`, `tools/routes/objects_test.sh`, issues #55/#68, claims C025 C026 | **done** | **`fn_0041a5a0` is hand-ported** -- the pass that collects every live object, depth-sorts it and draws its shadow, sprite, multiplier label, name tag and effects. It was ported because it clamps a name tag into the game's own 794-wide screen at four `MOV r32,0x31a` sites, and 0x31a is an IMMEDIATE in recompiled code: unlike the walk lock and the camera word there is no address to write, so owning the function is the only way to make that bound follow the view. The tag now stops at `view - 9` (1091 at a 1100 view, 969 at 978) instead of 785 at every width. The camera wrapper that used to sit in `background.c` is gone with it -- it existed only to fool the lifted `SUB reg, camera` sites, and the port reads `bg_draw_camera()` at each of them; the draw-time value is still never written back (issue #39). **Accepted on a gate built BEFORE it and shown to fail before it was trusted**: `tools/e2e.py objects` runs the port against the recompiled body at a 794 view -- where `bg_view_width()` is 794 and they must agree exactly -- in PIXELS and in STATE. The state arms are the ones that matter, because this pass writes back (the effects loop advances per-effect counters and decrements `obj[0x36c]`), so a port that drew right and counted wrong would pass a pixel-only check; `.data` and the 106 MB heap come out identical, which rests on claim C026 (guest state is deterministic to one byte in 106 MB, everything else being the security cookie and the CRT date string). Written from `re/instructions.tsv`, not the decompilation, which is wrong on three things that matter: the function ends `RET 0xc` and takes THREE stack args where Ghidra types two, every `fn_0043f010` call has an elided `__thiscall` receiver, and the second tag variant's string is a stack buffer holding "Com" that Ghidra rendered as a pointer into a bitmap resource. That elided receiver caused issue #68: the first port used the glyph sheet for impact clips; the listing loads the effect sheet at `0x0044f8fc`, and the corrected pass again matches the recompiled body in pixels and state. **Not covered by the gate:** the bracketed-name branch (slot mark == -1) and a multiplier above 9 did not occur in these runs |
| Drop-in coop | `runtime/overrides/coop.c`, `runtime/overrides/coop_debug.c`, `runtime/overrides/hud.c`, `tools/routes/coop_dropin_test.sh`, `tools/routes/coop_select_test.sh`, `tools/routes/two_human_match_test.sh`, issues #15 #16 #17 #19 #21 | **done** | **Works end to end, and always on** — there is nothing to switch on, because a feature nobody can find is not a feature: a device pressing for the first time while a match is *already running* claims a free player slot, and **chooses its character in its own HUD panel** — the candidate's portrait and bars appear in the empty box along the top and flash there, left/right cycle the game's own roster of 23, attack (A) locks in — and from the lock-in the pad drives it. **The fighter is not on the stage until then**: it cannot walk, be hit, or be seen, and the match carries on around a player who is still deciding (issue #19, which is what it looked like when the choice was made by a blinking body standing in the fight). A pad that is **unplugged** takes its fighter back out again, but only one this port put there. **How one slot can have a panel and no fighter**, which is the RE the feature turned on: the HUD strip (`fn_0041ae60`) and the stage pass (`fn_0041a5a0`) and the world step (`fn_004064d0`) all read the SAME per-slot byte, `this+4+i`, so not building the fighter leaves no panel to choose in and building it puts a body on the stage. The two passes have to disagree, and the only place they can is *between* them — `fn_0041ae60` is overridden in `hud.c` to raise the byte for a slot that is still choosing, call the game's own panel drawing, and put it back down. The panel that appears is therefore the game's own, its portrait and bars read off the record `coop.c` built, not a picture this port painted; and the fighter enters the world at exactly one point, the lock-in. The flash is that panel being drawn on an eight-frame period, *not* an alpha fade: the port's blit path is a colour-keyed copy of 8-bit paletted sprites with no blend anywhere in it, so fading would mean inventing per-object blending in the porting layer rather than using the game's. Cycling **rebuilds** the record (gate off, the game's own reset, the new data block, the same position) rather than swapping its data pointer, because animation frame numbers do not carry across characters — and that rebuild is where the panel's portrait, name and bars come from. Its device's buttons are withheld from the record until lock-in, so nothing is left in it on the frame the fighter arrives. Two-sided regression tests: `coop_dropin` asserts the pad's input reaches the joined record, `two_human_match` that input in a player record becomes movement — split that way because a fighter joining mid-fight gets knocked about, so displacement alone could not discriminate. The RE under it: `this` is `0x00458b00`; `this+404` is **400** object pointers on a `0x420` stride; an object is in the world iff the **byte at `0x00458b04 + index`** is 1 — read by `fn_004064d0`, by the stage's draw-list collection at `0x0041a5d0` and by the HUD strip alike; `this+2004` is the object-data registry — `data.txt`'s `<object>` list in file order — with each block's **id at +1780 and type at +1784** (0 = character), the type located by requiring a match on all 65 entries; `fn_004061d0` is the record's `__thiscall` reset and `+0x368`/`+872` its data pointer. The four-player cap is gone — the count comes from the device-selector table's own size. Player slot `i` **is** object index `i` — `fn_00419a60__orig` walks the selector and pointer tables in lockstep over eight entries — while a computer's fighter is unbound by that (one sits at index 11), which is why the joined mask tracks the character-select roster rather than the object slots. Two humans in a **match** are covered too (`tools/e2e.py two_human_match`) — a gap `controller_2p` never reached, since it stops at character selection. The joiner takes a slot the game's own roster considers empty, and the roster it cycles is the registry entries of type 0 less the template — 23 playable fighters; `LF2_COOP_CHAR` pins where the cycle STARTS so a test gets the same fighter every run, and is the only `LF2_COOP_*` name that is not purely a diagnostic. The joiner takes the **lowest slot with no fighter in it** — a second human is Player 2 — rather than the lowest whose device selector is zero, which put a joiner in slot 4 with P2's box empty beside it (issue #21) |
| Native overrides, split by subject | `runtime/overrides/` (`overrides.h`, `world.h`, `menu.c`, `screens.c`, `input.c`, `coop.c`, `coop_debug.c`, `text.c`, `objects.c`, `background.c`, `assets.c`) | **done** | The hand-written replacements for recompiled functions, divided by what the code is **about** rather than by which address it replaces — one screen's behaviour is usually spread over several overrides, and `fn_0043f010` alone draws every screen. The line that earns its keep is `coop.c` (what the game does) against `coop_debug.c` (how this port knows it did it): the input gather used to be a page of device routing buried in 250 lines of `LF2_COOP_*` instruments, now `coop_debug_tick()`. `world.h` is the game's object/player model — every address with the evidence that located it. Verified by re-running the drop-in selection before and after the split: the coop log lines are identical |
| Legacy netplay and replay | `runtime/win32/wsock.c`, issue #71 | **intentionally absent from the port flow** | The retired launcher is never entered, so its network and replay choices are unreachable. The loader still reaches the original binary's thread-creation import, which is deliberately a loud no-op; no socket protocol is implemented. Any future multiplayer is a separate custom rollback subsystem, not the game's protocol. |
| Startup crash | issue #47 | **fixed** | function ends now follow CONTROL FLOW, not Ghidra's declared size — `fn_00423480` reports 576 bytes and its body continues past that end, so the lifter emitted a function that ran off the end and returned with the frame still allocated. Reaching the end of a lifted body without a `RET` now aborts BY NAME rather than returning silently. Issue #47 carries the dozen ruled-out hypotheses, each with the measurement that ruled it out — do not re-test them |
| Rendering | `runtime/video/ddraw.c`, `runtime/win32/gdi.c`, `runtime/ui/device_icons.c` | **done** | Menus, screens, colour-keyed sprites, device SVGs, and game/GDI text render. Non-image text uses required embedded Liberation outline faces and output-resolution host tiles; the shipping-engine HiDPI route verifies 2x game glyph rasterization. Labels baked into original screen art remain bitmap assets and are tracked separately in issue #84. |
| Advertising removed | `runtime/overrides/menu.c`, `runtime/overrides/text.c` | **done** | panel + strips (`fn_00423b00`, descriptor `0x0044d060`) and the corner update notice (`fn_0043f010`, MENU_CLIP7 at 725,5) with its click target; verified by rect scan, no blits left in either region |
| Game flow | `docs/running.md` | **done** | reaches gameplay deterministically every run, by pad and by mouse+keyboard, on a presented-frame input schedule |
| Menus: one input model | `runtime/overrides/menu.c`, `runtime/overrides/screens.c`, `runtime/ui/settings_ui.cpp`, `runtime/ui/rmlui_input.cpp`, `runtime/app/pause.c`, `tools/routes/mouse_test.sh`, `tools/routes/settings_test.py`, `tools/routes/ui_escape_test.py`, `tools/routes/ui_global_test.sh` | **done** | Normal boot starts at the post-load **mode menu**. Mode selection, character selection, and the pre-fight overlay retain the game's own behavior. Escape/Start opens RmlUi directly everywhere; no legacy pause screen exists. A mouse-only route reaches a match, settings drives RmlUi by mapped controller actions, and the physical-input route drives it by XTEST keyboard and mouse; `ui_global` opens the same shell in all four contexts. The retired launcher has no reachable replay/network choices. |
| Game's own mouse cursor | `runtime/overrides/text.c` | **done** | declined in `fn_0043f010`; the host cursor is the only one. `LF2_CURSOR_ON=1` restores it |
| Audio: PCM integrity | `runtime/cpu/guest_map.h` | **done** | the surface arena used to overrun the sound arena and the game played bitmaps as audio; arenas are now declared in one place with build-time overlap checks and runtime bounds |
| Guest clock | `runtime/win32/imports.c` (`guest_ns`), `runtime/video/ddraw.c` (`frame_pace`), issue #18, claim C014 | **done** | Guest time is exactly **presented frames × 33.33 ms** plus the sleeps the game took, and never reads the wall — so how much of the game's timeline has passed by frame *N* is a property of the game, not of how fast or busy the machine is. The same route gives `charselect@906 overlay@1746 match@1968` on an idle box and under fourteen busy loops alike; with the old wall-derived clock the loaded run reached **no screen at all**. Real time moved to the **present**, which holds each frame until the wall reaches its due time: the guest counts, the host paces. Three load-bearing details, each found by its own failure — a `Sleep` is credited as a **floor** (`ms + 1`, because `Sleep(n)` returns after *at least* n, and crediting exactly *n* parks the game's pacer on its own boundary where it neither works nor waits); sleeps are credited **during play** as well as on the load's fast path (the startup waits produce no frames), which costs nothing once frames flow because 33.33 ms clears the 33 ms threshold the game compares against; and the pacer **drops its anchor while loading**, or every frame after the load is due far in the future. `LF2_CLOCK_SITES` (instrument I005) is what named the loop this had to accommodate |
| Load time | `runtime/overrides/assets.c` (`fn_004148a0`), `runtime/win32/imports.c`, issue #8 | **done** | **8.4-10.5 s -> 1.2 s** of active loading. Two causes, both measured: the frame-pacing `Sleep` between loader steps (skipped while loading, and the guest clock is credited so the wait ends instead of becoming a spin), and the game decrypting every data file **one byte at a time through `fscanf`/`fprintf`** — 2.5 M guest->host import calls per load, now a native loop proved byte-identical to the game's own on all 77 files. `LF2_LOAD_PROF=1` reports where the rest goes: 74% is drawing |
| Sprite colour-key | `recompiler/lift.c` | **fixed** | root cause was ADC/SBB dropping the carry; see below |
| Stage ground fill | `runtime/video/ddraw.c`, issue #9 | **fixed** | `DDBLTFX.dwFillColor` was read from offset 16 (`dwRotationAngle`), so every colour fill painted a leftover stack dword — the navy rectangles over stage 1-1's ground. It is at offset 80; verified on a stage 1-1 frame dump |

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
reuse an earlier blob (e.g. `game/bg/template/2/pic2.bmp` is byte-identical to `template/1`'s).
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
