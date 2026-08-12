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
| Verification: the suite | `CMakeLists.txt`, `runtime/overrides/geom.h`, `tests/test_geom.c`, `runtime/video/framelife.h`, `tests/test_framelife.c`, `runtime/app/framespec.h`, `tests/test_framespec.c`, `tools/e2e.sh` | **done** | **One ctest suite, ~1.3 s, and nothing in it boots the game.** That is a bar, not a description: a suite with a five-minute test in it stops being run, which is how the mouse route stayed green and broken for as long as it existed (issue #26). What CAN be checked offline is — `geom.h` holds the port's pure geometry (composition width, parallax, camera bounds and the wide-view centring, the pre-fight overlay's rows, the stereo pan) and `test_geom.c` walks all of it in a millisecond. The overrides **include** that header rather than keeping their own copy, so the test is exercising the code that ships, and `tools/e2e.sh background`'s byte-identity arm confirms the factoring changed no pixel. The **renderer's frame lifetime** followed the same route (`framelife.h` + `test_framelife.c`): when a display list is cleared, how a frame held up under the pause menu is rewound, which pooled texture serves a tile — all bookkeeping over plain integers, and every bug in the pause menu's first cut was in it, each one found by looking at a 1080p screenshot after a five-minute route run (issue #53). The **frame-spec grammar** went the same way (`framespec.h` + `test_framespec.c`, 26 checks): `LF2_FRAME_DUMP` now takes `@screen+N` as well as a frame number, and a bug in that parser neither crashes nor prints — the frame is simply not dumped and the route reports "the run never reached it", which reads as a problem with the game. Its negatives were **vacuous on the first cut** and the test says so: a mutant of the header that ignored the unresolved flag passed all 26 checks, because the stub returned −1 for an unresolved anchor and −1 is never a frame anyone asks about. The stub now returns a plausible frame there, and the mutant fails — run against both classes before the test was believed. The audio pan is the worked example: a three-run, 270-second script became 20 assertions, and gained a walk across every on-screen pixel at 794 and 1920 that the script never did. The scripts that DO boot the game live behind `tools/e2e.sh` (all of them, or by name), one at a time because each wraps its instance in a wall-clock `timeout`; they answer only what needs a running game — does a route reach a screen, does a second pad drive its fighter, does the GPU renderer match the software one |
| Instruction differential | `tests/test_insn.c` | **done** | 8373 encodings x 8 rounds = 66,984 checks vs the host CPU, incl. x87; negative-control validated |
| Recompiler: decoder | `recompiler/x86_decode.c` | **done** | length-exact on all 70,508 instructions; negative-control validated |
| Recompiler: lifter (x86 → C) | `recompiler/lift.c` | **done** | 74,135 / 74,136 lifted (100.00%); 1 TODO is decoded data, see below |
| Runtime | `runtime/cpu/` | **done** | CPU state, lazy flags, 4 GiB lazily-committed memory, PE load, import binding; ~13% of a core in play |
| Native renderer | `runtime/video/render.c`, `runtime/video/render.h`, `runtime/video/hd2d.c`, `runtime/video/mesh.c`, `runtime/shaders/`, `tools/routes/render_test.sh`, `tools/routes/mesh_test.sh`, `tools/build/build_shaders.sh`, issues #30–#32 #49 #62 | **wip** | The game's draws as **GPU geometry**. `ddraw.c` records a display list per destination surface; the game composes off-screen and copies to the primary, so the source of that copy *names* the composition and the frame boundary is discovered from the game's own blits with no address hardcoded. **The colour key becomes alpha** on upload — the port's first blend stage, the thing claim C010 said made shadows impossible. **The picture fills the window, and it is scaled PER QUAD** (issue #41): the window sets a world scale of `min(win_h/550, win_w/794)` and a composition of `win_w/scale` floored at 794, so the height buys magnification and the leftover width buys FIELD OF VIEW. Each quad in the display list is scaled as it is drawn into a full-resolution target (`SDL_SCALEMODE_NEAREST` throughout), so the geometry stays exact at float precision and only a sprite's own texels are resampled — once, from the source art. That is what distinguishes it from the upscale the port removed, which composed small and let SDL blow the finished frame up, quantising text and lighting to the small grid first; at 16:9 the two produce the same composition WIDTH and nothing else the same. The height cannot buy field of view — LF2's vertical axis carries z and jump height, both fixed by stage data, and every layer's picture is 550 rows — which is why it buys scale instead. At 794x550 the scale is exactly 1 and every byte-identity arm still holds. The separate whole-number OBJECT magnification is gone with it: it put a 2x fighter on a 1x floor. The software compositor can only stretch its one finished buffer to the same rectangle, and is the fallback. GDI text rides as premultiplied **tiles** at their real place in the list. The renderer is created as SDL's **`gpu`** backend by name: the OpenGL backend SDL would otherwise pick has no `SDL_GPUDevice`, hence no `SDL_GPURenderState` and no shaders (C020). **Isometric lighting and sprite-cast shadows** (`hd2d.c`) in real fragment shaders — one key light as a direction in the stage's own axes (x across, y up = LF2's jump axis C018, z toward the camera), hemisphere ambient, and a bevel normal from the gradient of the sprite's silhouette — applied **only to the objects standing in the stage**, which the game identifies for free by drawing a shadow ellipse at their feet immediately before them (C019). HUD and text are untouched. The shadow is that sprite's silhouette sheared along the **same** light vector through `SDL_RenderGeometry`, **crisp** (a blurred half-res mask of a 32-pixel sprite is a shapeless smear), into a mask needing its own shader because SDL's fixed-function multiply gives `sprite.rgb * a` — the fighter's colours — and made a shadow darker under the bright parts of them. **The floor is lit as a floor:** `bg.dat`'s `zboundary:` is where the walkable floor begins on screen, because the depth axis projects straight down it (C021, validated on 12 of 12 stages); floor and backdrop see different amounts of sky, applied as **colour only** at locked luminance, gated on the in-match HUD. Shaders are **committed SPIR-V** (`tools/build/build_shaders.sh`, `ctest shaders` fails on a stale blob), so the build still needs only a C compiler and SDL; a backend that cannot take SPIR-V says so and gets the plain composition, with no approximation to fall back on. `tools/e2e.sh render` runs four arms: the GPU frame matches the software compositor to **max 1–2 levels of 255**, dropping every 7th draw must differ, the light must change the **match** frame, and it must change **nothing** on the menu frame — that last one is what catches an effect spreading over the whole picture. **Removed, and worth recording:** a bloom, depth of field, atmospheric haze, vignette and colour grade all shipped here briefly and were cut — each touched every pixel and together they read as a filter over a screenshot. The bloom's bright pass was `SDL_BLENDMODE_MOD` of the frame over itself, which is squaring, not a threshold. **Not done:** the stage is still flat painted layers (the parallax rate is the only other depth in the data, and it already *is* the projection the game draws with), and **the port's own UI is in the frame** rather than beside it (issue #52). The controls hint and the pause menu used to draw pixels onto the primary after the composition had been copied to it, so the renderer had to be switched off for any frame carrying one — which was every screen outside a match, **0 of 901** frames GPU-drawn at 1080p. Both now record into the display list: the hint appends glyph tiles to the live composition, and the pause menu is drawn over a **retained frame**, because pause freezes the game by declining to call its update and no blit arrives while it is up. A frame's lists are therefore cleared by the first call that RECORDS over them, not by the present that finished them. The port's UI is marked in the list and drawn **after** `hd2d_post`: put before it, the character mask built from the game's own objects re-composited a fighter on top of the pause panel at full brightness. The tile pool is keyed on a 32-pixel bucket and a texture may serve any tile that fits in its corner — keying on the exact size rested on 'a tile's size repeats constantly', which is true of a glyph cell and false of `TextOutA`, whose tile is a whole string **A DEPTH-TESTED GEOMETRY PASS EXISTS** (`mesh.c`, issues #49/#62), which is what the hand-woven stage sets need and what the 2D path cannot give them: `SDL_render.h` has no depth attachment at all, and the display list is painter-sorted — right for LF2's sprites, which the game sorts and which never interpenetrate, wrong for a mesh, which interpenetrates itself. It is **additive rather than a rewrite**: `SDL_GetGPURendererDevice` hands back the device this renderer is already built on (C029), the pass renders into its own colour target and a D32_FLOAT depth target, and the colour target is wrapped as an ordinary `SDL_Texture` through `SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER` — the *same object*, no copy and no readback (C030) — for the display list to place. A set needs no depth shared with the sprites because it is behind all of them; the interpenetration that would need one is mesh-against-mesh, entirely inside the pass. It ships a **self-test that a broken depth test cannot survive**: a near triangle submitted FIRST with a far one drawn over it, reading back two pixels — the overlap (must be the near colour) and a far-only pixel (must be the far colour, which is what rules out a pass that drew nothing at all). Run against both classes: with `enable_depth_test` false the overlap pixel comes back the far colour and it prints FAIL. It runs at **init**, not from `render_report`, which is behind `LF2_RENDER_DEBUG` and fires every 900 frames — a self-test needing two switches and a long run is the same as not having one. `tools/e2e.sh mesh` runs it. **Nothing submits geometry yet** and `mesh_report` says so rather than printing a clean zero; the next pieces are the stage-space camera and the authored-data format, neither blocked by the renderer any more. `build_shaders.sh` now compiles `*.vert` as well as `*.frag`, taking the stage from the extension. **The projection is the GAME'S, derived not chosen, and it is not a matrix.** A layer at parallax depth *d* shifts by `camera/d` (C031); a fighter's z is used *directly* as a screen row, so the depth axis projects down the screen at slope exactly 1 and jump height subtracts from it (C018); but every object shifts by the camera **flat** whatever its z — the game gives a fighter at the near zboundary and one at the far the same parallax rate. That last fact is why no perspective camera can reproduce this and why LF2 magnifies by exactly 1 at every depth. And `X - camera/d` is not linear in `(X, d, 1)`, so a 4×4 with a perspective divide cannot express it either — it gives `X/d`. The depth therefore rides **per vertex** and the divide happens per vertex, which is why a `MeshVertex` carries **four** independent numbers (x, jump, row, depth) and collapsing `row` and `depth` authors against a camera the game does not have. `geom_stage_project`/`geom_stage_clip` are the copy under test; `mesh.vert` is a transcription and says so. The acceptance test everything rests on: a quad at a layer's derived depth lands where `geom_layer_offset` — the game's own placement, a different expression — puts that layer's picture, checked on seven real layers of real stages at five camera positions. **The layer depths were already in the shipped data** (C031): a layer's scroll rate is a perspective divide written as a ratio, so `depth = 1/rate`; it reproduces every stage's drawing order and predicts that The Great Wall's `road3` is *in front* of the fighters, which it is. Nobody authors a depth for existing art. **Both bridges are measured, both classes**: the pass renders into a colour target wrapped as an ordinary `SDL_Texture` (C030) and can sample the GPU handle behind textures `render.c` already uploaded (C032), so the stage's art exists once — with the recorded hazard that the handle must be read at bind time, never cached. **Left before a stage can be woven:** texture support in the pass, and the authored-data format. |
| Runtime (SDL3) | `runtime/video/ddraw.c`, `runtime/win32/`, `runtime/input/gamepad.c`, `runtime/audio/` | **done** | video / input / Win32 shim; effects via DirectSound, music via ffmpeg. The *software* video path is SDL's 2D renderer, not a GPU pipeline (the native renderer above is) — the frame is composed on the CPU by the software blitter and presented as ONE streaming texture (`SDL_CreateRenderer(window, NULL)`, then `SDL_RenderTexture(NULL, NULL)` per frame). No render target, no shader, no depth buffer, and no alpha/blend path anywhere (claim C010). This row used to say “SDL3 GPU”, which read as though geometry reached the GPU; it does not, and issue #30 is where that changes **Widescreen is CENTRED and no longer silences the right** (issue #39): the game centres the players' centroid in a 794-wide window (`SUB ESI,0x18d` at `0x0041bb7d`) and pans sound between two screen speakers at x 200/600 reaching 400 px — both are the 794 screen written down as pixels. The world is now drawn from a camera shifted left by half the extra width (a *draw-time* value: writing it back feeds `fn_0041b5d0`'s 1/7 easing and drifts to `target - 7K`), and the pan constants are scaled by `view/794`. `ctest geometry` walks every on-screen x at 794 and 1920 and has the unscaled constants as the negative arm **The port drives itself into any of the game's eight modes** (`LF2_MODE=<name>`, `runtime/overrides/menu.c`): it writes the game's own mode-menu selection and lets the route's confirm dispatch it. Before this every scripted route took whatever the menu was sitting on — VS — so seven of eight modes were untested and the stage-mode camera lock could not be verified at all. `tools/e2e.sh stage_mode` uses it, with the VS run as the negative that proves the lock is a stage-mode signal **The mouse drives the game into a match on its own** (`tools/e2e.sh mouse`: charselect → overlay → match, no key, no pad). Two port bugs were in the way and neither was visible in a screenshot: the pre-fight overlay's rows were a uniform 24-px step measured off three sampled highlight blits, where Ghidra on `FUN_00429730` gives them as 16/39/64/87/111/137 on a slant (claim C022 — the three rows sampled are exactly the three a uniform step gets nearly right); and an idle pointer counted as a *move* on the frame a screen opened, because each handler's last-position memory belonged to the handler and not the screen, so the overlay opened and immediately selected whatever the resting pointer was over. `tools/re/ghidra_scripts/DecompDump.py` is how a function gets decompiled now — see docs/running.md |
| Controllers | `runtime/input/gamepad.c`, `runtime/overrides/input.c` | **done** | SDL3 gamepad; auto-detect, hotswap, and the pad merged into the game's own player buttons by the ported input gather — no configuration, keyboard still live, a second pad joins as Player 2. Regression-tested pad-only by `tools/routes/controller_test.sh` and `tools/routes/controller_2p_test.sh` |
| Input path | `runtime/win32/win32.c` | **done** | keyboard and mouse verified into game state; menu navigates |
| Window modes | `runtime/win32/win32.c` | **done** | windowed / borderless / fullscreen, Alt+Enter toggle |
| Widescreen | `runtime/video/ddraw.c`, `runtime/win32/win32.c`, `runtime/win32/gdi.c`, `runtime/overrides/menu.c`, `runtime/overrides/background.c`, `tools/routes/widescreen_test.sh`, `tools/routes/background_test.sh`, `tools/routes/resize_test.sh`, issues #13 #20 #23 #28 #29 #30 | **wip** | **The window decides**, live, on the frame after a resize — there is no switch, because an env var read once at startup is a developer's escape hatch rather than a feature (issue #20). The composition follows the window's **aspect**, not its pixel width: a 1920x1080 window gets `550*1920/1080 = 978` of world scaled up to fill it, and a window narrower in aspect than 794x550 clamps to 794 and letterboxes. The surfaces that follow are allocated **once** at `WIDE_MAX` with their **pitch fixed**, and a resize only moves `s->w` — `vram_alloc` has no free, so reallocating per resize event would exhaust the arena during one drag of an edge; `Lock` reports w/h/pitch fresh every call, so the game picks the change up next frame. `tools/e2e.sh widescreen` asserts the whole table including the two cases that must NOT widen. What it gives is a genuinely wider field of view, not a scaled picture: the compose surface widens and the game's own viewport-width words are set to match, so the camera and the layer loops draw MORE WORLD. Everything fixed at 794 is **framed per screen** (issue #44): the front end and the mode menu are LEFT-aligned because their character portrait is drawn at a hard literal x=0 and hangs on the screen's left edge, the loading screen is centred with its side bands extended from its own edge columns (its backdrop is a picture, so that extension is a declared port choice and the run says so), and character selection stays centred. A screen is recognised by the full-screen fill colour it paints — 0x10206c, 0x122565, 0x000000, the first two appearing exactly once each in the whole binary — rather than by a .data word, because the word previously taken for the mode menu's cursor is the game MODE and reads 1/4/5 in a match (issue #51). Everything else fixed at 794 is **centred** — front end, mode menu, character select, overlay and the in-match HUD strip. The centring is applied **while composing**, to the draws that fit inside the game's own 794-wide screen, *not* to the backbuffer→primary copy, and that is what lets a screen's own full-screen colour fill act as the **background**: it spans the composition from the left edge with the artwork centred on top (issue #42, a very wide short window used to jam the front end against the right with black down the left). Only a flat colour is extended — a backdrop that is artwork is not, because stretching a picture invents layout. A 1:1 copy also writes every column of the primary every frame, removing issue #29's ghost by construction. Which fills are the STAGE's full-width bands is marked by the background override (`world_band_hint`, `LF2_BAND_DEBUG`) rather than guessed from the rectangle — the game's one fill helper `fn_00415160` serves both the stage's tinted layers and the front end's backdrop, so `0..794` matched the menu exactly (offset during composition, with GDI text in the HUD band following it). Full-width colour fills and single-blit backdrops are carried across the viewport. The **background layers are not yet right**, and the mechanism is now fully read out of the game (claim C017): a layer has a SPAN (`width:`) and an optional LOOP (`loop:`), and `fn_0041a250` draws it at `off = -(camera*(span-794))/(stage_width-794)`, tiling by `loop` up to the span when there is one and drawing once when there is not. A span is authored so the layer covers the 794 screen at **every** camera position with no margin, so a **non-looping layer has no picture left over for a wider view at any camera** — every stage's sky is non-looping. The whole pass is the override `runtime/overrides/background.c`, and the widescreen change in it is one substitution: the game's literal 794 becomes the live view width, in the parallax and in the tiling bound. A **looping** layer is carried past 794 at its own `loop:` step — the stage's layout continued; a **non-looping** one is drawn once and pinned, because there is no more picture. The camera is clamped to `stage_width - view_width` in the same file (issue #28, resolved), so a wide view no longer scrolls past the wall a character can walk to. `ddraw.c`'s contiguity heuristic is **deleted** — it could not tell the two layer kinds apart and repeated Brokeback Clif's middle cliff across the band with hard seams. `tools/e2e.sh background` holds all of it: byte-identical to the recompiled body at 794x550, differing at 1600x550, with a skewed-parallax arm proving the identity check can fail. A resize no longer leaves the previous size's picture standing beside a centred screen: the centring offset is applied to the full-width copy to the primary, so it never writes the leftmost `offset` columns, and those are cleared when the geometry moves (issue #29, `tools/e2e.sh resize`, with the clear disabled as the negative). What is left is the black beside the BACKMOST layer in a very wide view, and it is now measured rather than described: on 6 of 12 stages that layer is a static 794-800 px picture whose span EQUALS its picture width, so a 1920x1080 window leaves 178-184 of its 978 columns genuinely empty. The `span <= view` predicate an earlier plan hung on is wrong — it also catches CUHK's 180 px patch of grass and Queen's Island's 127 px lamp post, which are props with the next layer behind them, not backdrops out of picture. Asked with those numbers, the reporter chose to **hand-weave** the missing columns rather than stretch, tile, mirror, edge-clamp or letterbox them, so issue #23 folds into issue #62 and hands over its per-stage work order. The camera is still clamped to `stage_width - 794` too, so a wide view scrolls past the stage's walls (issue #28, open). Both are the same literal 794 and land together; `LF2_BG_TABLE=all` + `tools/re/bg_table_check.py` (instrument I006) is what the fix reads its numbers from, checked 12/12 against the shipped `bg.dat`  **HiDPI is handled and VERIFIED on a simulated 4K panel** (issue #56): the window carries `SDL_WINDOW_HIGH_PIXEL_DENSITY`, the geometry is seeded from `SDL_GetWindowSizeInPixels` rather than the point size, and the pointer — which SDL delivers in POINTS — is multiplied by the density in `geom_pointer_to_compose`. `kwin_wayland --virtual --width 3840 --height 2160` with `kscreen-doctor output.1.scale.2` is a real scaled display: the port reports `794x550 points -> 1588x1100 pixels (display scale 2.00)` and composes 794 columns of world at scale 2.000 into the whole drawable. `tools/e2e.sh hidpi` is that run and REFUSES when the output comes up unscaled; `ctest geometry`'s `test_density` walks five densities offline and asserts the invariant across them — same field of view, world scale × density, the same window POINT on the same composition pixel — because a port composing from the point size has a self-consistent round trip at every single density. Two simulations that do NOT work are recorded in the entry: Xvfb at any DPI (SDL's X11 backend reports points == pixels by construction) and kwin's own `--scale`, which is windowed-mode only. |
| Pause menu | `runtime/app/pause.c`, `runtime/overrides/screens.c`, issue #22 | **done** | Escape or Start during a match. Built on declining to call `fn_004246b0__orig`, which freezes the world with nothing to save or restore; the present lives inside that body, so it is issued separately, and the frozen frame is snapshotted because the menu draws straight onto the primary. **RESUME / DROP OUT / LEAVE MATCH / QUIT GAME**, on mouse, keyboard and pad. Escape is withheld from the game during a match so its own quit prompt does not open underneath. The rows are built per pause, because **DROP OUT is per player**: it appears only when the device that OPENED the menu is driving a slot this port put there (`coop_owns`), and it calls the same `coop_leave` that an unplugged pad does — so the menu records which device paused rather than guessing. **LEAVE MATCH** drives the game's own way out — F4, then the pre-fight overlay's own Exit item, with the selection placed and the GAME's attack button dispatching it — and lands on the character-select screen with the roster cleared. It **lands on the game's own front-end menu** (screen word `0x0044d020` = 10, `fn_00431d10`) and issue #22 is closed. It used to overshoot to character selection, and the cause was its own press: `fn_00431c70` clears the held-button latch `fn_00431b70` edge-detects against on every way out of a match, so the second gather of a two-gather synthetic confirm read as a FRESH press on the menu that had just appeared and confirmed whatever the cursor was on. A synthetic press now belongs to the screen it was issued on — the game's own rule, not a shorter frame count. `tools/e2e.sh exit_to_menu` asserts the landing by the screen WORD, because the two candidate screens share a blit destination and cannot be told apart from a frame dump. Both unpause FIRST — pausing works by declining to call the game's update, so a transition driven while frozen would be delivered to a game that never runs another frame. The confirm is issued through a device that is actually driving a player, because inside the game proper a device with no slot has nowhere for its buttons to go |
| Object pass | `runtime/overrides/objects.c`, `tools/routes/objects_test.sh`, issue #55, claims C025 C026 | **done** | **`fn_0041a5a0` is hand-ported** -- the pass that collects every live object, depth-sorts it and draws its shadow, sprite, multiplier label, name tag and effects. It was ported because it clamps a name tag into the game's own 794-wide screen at four `MOV r32,0x31a` sites, and 0x31a is an IMMEDIATE in recompiled code: unlike the walk lock and the camera word there is no address to write, so owning the function is the only way to make that bound follow the view. The tag now stops at `view - 9` (1091 at a 1100 view, 969 at 978) instead of 785 at every width. The camera wrapper that used to sit in `background.c` is gone with it -- it existed only to fool the lifted `SUB reg, camera` sites, and the port reads `bg_draw_camera()` at each of them; the draw-time value is still never written back (issue #39). **Accepted on a gate built BEFORE it and shown to fail before it was trusted**: `tools/e2e.sh objects` runs the port against the recompiled body at a 794 view -- where `bg_view_width()` is 794 and they must agree exactly -- in PIXELS and in STATE. The state arms are the ones that matter, because this pass writes back (the effects loop advances per-effect counters and decrements `obj[0x36c]`), so a port that drew right and counted wrong would pass a pixel-only check; `.data` and the 106 MB heap come out identical, which rests on claim C026 (guest state is deterministic to one byte in 106 MB, everything else being the security cookie and the CRT date string). Written from `re/instructions.tsv`, not the decompilation, which is wrong on three things that matter: the function ends `RET 0xc` and takes THREE stack args where Ghidra types two, every `fn_0043f010` call has an elided `__thiscall` receiver, and the second tag variant's string is a stack buffer holding "Com" that Ghidra rendered as a pointer into a bitmap resource. **Not covered by the gate:** the bracketed-name branch (slot mark == -1) and a multiplier above 9 did not occur in these runs |
| Drop-in coop | `runtime/overrides/coop.c`, `runtime/overrides/coop_debug.c`, `runtime/overrides/hud.c`, `tools/routes/coop_dropin_test.sh`, `tools/routes/coop_select_test.sh`, `tools/routes/two_human_match_test.sh`, issues #15 #16 #17 #19 #21 | **done** | **Works end to end, and always on** — there is nothing to switch on, because a feature nobody can find is not a feature: a device pressing for the first time while a match is *already running* claims a free player slot, and **chooses its character in its own HUD panel** — the candidate's portrait and bars appear in the empty box along the top and flash there, left/right cycle the game's own roster of 23, attack (A) locks in — and from the lock-in the pad drives it. **The fighter is not on the stage until then**: it cannot walk, be hit, or be seen, and the match carries on around a player who is still deciding (issue #19, which is what it looked like when the choice was made by a blinking body standing in the fight). A pad that is **unplugged** takes its fighter back out again, but only one this port put there. **How one slot can have a panel and no fighter**, which is the RE the feature turned on: the HUD strip (`fn_0041ae60`) and the stage pass (`fn_0041a5a0`) and the world step (`fn_004064d0`) all read the SAME per-slot byte, `this+4+i`, so not building the fighter leaves no panel to choose in and building it puts a body on the stage. The two passes have to disagree, and the only place they can is *between* them — `fn_0041ae60` is overridden in `hud.c` to raise the byte for a slot that is still choosing, call the game's own panel drawing, and put it back down. The panel that appears is therefore the game's own, its portrait and bars read off the record `coop.c` built, not a picture this port painted; and the fighter enters the world at exactly one point, the lock-in. The flash is that panel being drawn on an eight-frame period, *not* an alpha fade: the port's blit path is a colour-keyed copy of 8-bit paletted sprites with no blend anywhere in it, so fading would mean inventing per-object blending in the porting layer rather than using the game's. Cycling **rebuilds** the record (gate off, the game's own reset, the new data block, the same position) rather than swapping its data pointer, because animation frame numbers do not carry across characters — and that rebuild is where the panel's portrait, name and bars come from. Its device's buttons are withheld from the record until lock-in, so nothing is left in it on the frame the fighter arrives. Two-sided regression tests: `coop_dropin` asserts the pad's input reaches the joined record, `two_human_match` that input in a player record becomes movement — split that way because a fighter joining mid-fight gets knocked about, so displacement alone could not discriminate. The RE under it: `this` is `0x00458b00`; `this+404` is **400** object pointers on a `0x420` stride; an object is in the world iff the **byte at `0x00458b04 + index`** is 1 — read by `fn_004064d0`, by the stage's draw-list collection at `0x0041a5d0` and by the HUD strip alike; `this+2004` is the object-data registry — `data.txt`'s `<object>` list in file order — with each block's **id at +1780 and type at +1784** (0 = character), the type located by requiring a match on all 65 entries; `fn_004061d0` is the record's `__thiscall` reset and `+0x368`/`+872` its data pointer. The four-player cap is gone — the count comes from the device-selector table's own size. Player slot `i` **is** object index `i` — `fn_00419a60__orig` walks the selector and pointer tables in lockstep over eight entries — while a computer's fighter is unbound by that (one sits at index 11), which is why the joined mask tracks the character-select roster rather than the object slots. Two humans in a **match** are covered too (`tools/e2e.sh two_human_match`) — a gap `controller_2p` never reached, since it stops at character selection. The joiner takes a slot the game's own roster considers empty, and the roster it cycles is the registry entries of type 0 less the template — 23 playable fighters; `LF2_COOP_CHAR` pins where the cycle STARTS so a test gets the same fighter every run, and is the only `LF2_COOP_*` name that is not purely a diagnostic. The joiner takes the **lowest slot with no fighter in it** — a second human is Player 2 — rather than the lowest whose device selector is zero, which put a joiner in slot 4 with P2's box empty beside it (issue #21) |
| Native overrides, split by subject | `runtime/overrides/` (`overrides.h`, `world.h`, `menu.c`, `screens.c`, `input.c`, `coop.c`, `coop_debug.c`, `text.c`, `objects.c`, `background.c`, `assets.c`) | **done** | The hand-written replacements for recompiled functions, divided by what the code is **about** rather than by which address it replaces — one screen's behaviour is usually spread over several overrides, and `fn_0043f010` alone draws every screen. The line that earns its keep is `coop.c` (what the game does) against `coop_debug.c` (how this port knows it did it): the input gather used to be a page of device routing buried in 250 lines of `LF2_COOP_*` instruments, now `coop_debug_tick()`. `world.h` is the game's object/player model — every address with the evidence that located it. Verified by re-running the drop-in selection before and after the split: the coop log lines are identical |
| Scripted routes | `runtime/input/gamepad.c`, issue #18, claim C011, instrument I004 | **done** | `LF2_VIRTUAL_PAD` scripts take `button@<screen>[+n]` (`frontend`, `charselect`, `overlay`, `match`) as well as `button:<frame>`, firing off the game's own drawing so a press aimed at the match lands there however long the load took; **`LF2_FRAME_DUMP` takes the same anchors** — it did not, and a dump aimed at frame 2250 to get "a frame with fighters on it" quietly became a different moment the instant anything upstream moved. **The opening press is `@frontend`**, which is drawn on frame 1: every route used to open with `south:900`, a number chosen to clear a load that does not start until a mode is confirmed, so 840 frames of every run were the game idling on its first screen (issue #57). Note `@match` means the HUD is up, **not** that fighters are drawn — the stage load runs under it — so a route asserting on gameplay needs margin, not a tight offset; `virtual_pad_report()` names the screens a route reached and says outright when a press never fired. A scripted run also **ignores physical controllers** — an attached pad binds gamepad slot 0, the front end is driven from slot 0 only, and a real Xbox pad plugged in for play silently stalled every route test at the front end (`input: 0 gathers`, no screen reached). That was very nearly written down as CPU contention |
| Netplay | `runtime/win32/wsock.c` | **stubbed** | reports started-but-no-network, which the game handles |
| Startup crash | issue #47 | **fixed** | function ends now follow CONTROL FLOW, not Ghidra's declared size — `fn_00423480` reports 576 bytes and its body continues past that end, so the lifter emitted a function that ran off the end and returned with the frame still allocated. Reaching the end of a lifted body without a `RET` now aborts BY NAME rather than returning silently. Issue #47 carries the dozen ruled-out hypotheses, each with the measurement that ruled it out — do not re-test them |
| Rendering | `runtime/video/ddraw.c`, `runtime/win32/gdi.c` | **done** | menus, screens, GDI text and colour-keyed sprites all render; GDI text is anti-aliased through a system font when `SDL3_ttf` is present |
| Advertising removed | `runtime/overrides/menu.c`, `runtime/overrides/text.c` | **done** | panel + strips (`fn_00423b00`, descriptor `0x0044d060`) and the corner update notice (`fn_0043f010`, MENU_CLIP7 at 725,5) with its click target; verified by rect scan, no blits left in either region |
| Game flow | `docs/running.md` | **done** | reaches gameplay deterministically every run, by pad and by mouse+keyboard, on a presented-frame input schedule |
| Menus: one input model | `runtime/overrides/menu.c`, `runtime/overrides/screens.c`, `tools/routes/mouse_test.sh` | **done** | every screen from the launcher to the match takes mouse, keyboard and pad: launcher (screens 0/6/7), the post-load **mode menu**, **character selection** (hover moves the slot cursor, click joins and picks) and the **pre-fight overlay** (`0x0044d06c`, phase word `0x0044d070`). A mouse-only route reaches a match with no key and no pad |
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
