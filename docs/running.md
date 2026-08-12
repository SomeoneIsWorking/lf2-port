# Running the port

```sh
cmake -S . -B scratch/build && cmake --build scratch/build -j
cd game && ../scratch/build/lf2 lf2.exe
```

The working directory must be the extracted game tree — the game opens its data with
relative paths.

## macOS

**Untested — no Mac was available.** What follows is what the code should need, and the
portability blockers that have been removed, not a report of a successful build.

```sh
brew install sdl3 cmake
cmake -S . -B scratch/build && cmake --build scratch/build -j
cd game && ../scratch/build/lf2 lf2.exe
```

The runtime is POSIX plus SDL3 throughout; an audit found no `/proc`, no epoll, no
Linux-only headers. Real blockers found and fixed:

- `MAP_NORESERVE` does not exist on macOS. It is only ever a hint on Linux, so it is now
  defined to zero where absent.
- `MAP_32BIT` is a Linux extension, used by the instruction differential to keep guest
  memory addressable with a 32-bit base register. Where it is absent the mapping is
  requested by hint and **checked**, because a hint is advisory — a mapping that lands high
  is unmapped and the next hint tried, and if none land the test skips saying so rather
  than comparing against a base the host cannot address. Build with `-DLF2_NO_MAP_32BIT` to
  run that path on Linux; it was, and it gives the same 66,984 checks and 0 mismatches.
- A `#define MAP_32BIT 0` used to paper over the first point, on the unmeasured reasoning
  that macOS "already places mmap low enough". It would have made the portable path
  unreachable on the one platform it exists for. Removed.

### Building with clang found a real bug

macOS means clang, so the whole tree is now built and tested under clang as well as gcc.
That immediately failed: **40 mismatches in the instruction differential, all `FSTP ST(i)`**
— and 0 under gcc.

The lifter emitted `FST(i) = fpu_pop();`. `FST(i)` is `cpu.st[(cpu.st_top + i) & 7]`, so
the left side *reads* `cpu.st_top` while the right side *modifies* it, with nothing
sequencing them. That is undefined behaviour, not a clang quirk: gcc happened to evaluate
the destination address first, which is the correct order, and clang did not. It is emitted
as two statements now.

This is worth stating plainly because it was invisible for the life of the project: the
differential passed 66,984 checks every time it ran, and the game rendered correctly. A
second compiler was the instrument that could see it.

`tools/build/build_matrix.sh` builds and tests every compiler on the machine at two optimisation
levels — evaluation order is the front end's choice and can differ with `-O` too — so this
stays a routine check rather than something done once. Pass `-R instructions` for just the
differential; the whole suite is a second and a half. It warns when fewer than
four configurations ran, because a matrix that quietly tested one thing is not
cross-checking anything.

Validated by re-introducing the bug: gcc passes at both levels, clang fails at both with
the same 40 mismatches, and the script exits non-zero. A matrix that reported success while
a configuration failed would be the worst possible version of this.

**A compiler warning cannot replace it, and this was checked rather than assumed.** Fed the
exact defect — `FST(i) = fpu_pop();` with `fpu_pop` a `static inline` that modifies
`cpu.st_top` — both `clang -Wunsequenced` and `gcc -Wsequence-point` are **silent**, while
both flag a syntactic `i = i++` control in the same file. They only handle syntactic cases.
A clean warning sweep over the generated code is not evidence about this bug class.

**Apple Silicon now runs the instruction differential — against recorded x86 truth.**
The recompiled game is ordinary C and compiles for arm64 like anything else, but two
tests compare against the host: the instruction differential executes the binary's *own*
x86 bytes, and the flag test computes reference flags with x86 inline asm. Neither
comparison exists on an ARM CPU — and an arm64 build previously ran with its instruction
semantics entirely unverified, which is exactly where physics-class bugs (a character
walking in place, a jump that never lands) would live with no instrument to see them.

The instrument now exists: **golden vectors**. On x86, `test_insn --capture
re/insn_vectors.bin` records the host CPU's outputs for every corpus case (the committed
file holds 8,373 cases × 8 rounds); on any host, `test_insn --replay <file>` runs the
lifted C on identically regenerated inputs and compares. On a non-x86 host the plain
`test_insn` run becomes this replay automatically, so `ctest` on a Mac now performs
66,984 real comparisons instead of a skip. Cases are matched by instruction bytes, and
each case's input stream is seeded from those bytes, so the corpus differing between
machines (Ghidra dump here, self-derived there) does not misalign anything; replay
reports how many cases had no vector, and a replay that matched nothing fails rather
than passing. Validated in both directions: corrupting recorded registers produces
register mismatches, corrupting recorded x87 slots produces `st(i)` mismatches, and a
truncated or missing file refuses rather than comparing nothing.

The flag test still skips on non-x86 (exit 77, which ctest reports as a skip). The
decoder test is pure C and runs anywhere. An earlier version of this note claimed the
flag test was pure C too; the first arm64 build failed on its asm constraints, so it
plainly was not.

The other thing to expect on Apple Silicon is the x87 question in `docs/isa-scope.md`:
host `long double` is 128-bit quad there rather than the 80 bits it is on x86-64. The port
uses `double` for x87 precisely because neither matches, so this should be a non-issue —
but it is the first thing to check if float behaviour differs.

## Headless runs and screenshots

Forcing the video driver is required, and the reason is worth knowing:

```sh
Xvfb :99 -screen 0 1024x768x24 &
cd game && DISPLAY=:99 SDL_VIDEODRIVER=x11 ../scratch/build/lf2 lf2.exe &
DISPLAY=:99 import -window root shot.png
```

**Without `SDL_VIDEODRIVER=x11` the capture comes out black, and the port is not at
fault.** SDL selects its Wayland backend whenever a compositor is reachable, so the window
opens on the real desktop while `import` photographs the empty Xvfb display. Unsetting
`WAYLAND_DISPLAY` is not enough — SDL still finds the socket. This looked like a rendering
bug for a while; it is purely an artefact of the capture setup.

## Text and the font

Part of every frame is drawn through GDI, with what on Windows is the device context's
default proportional font. That was rendered here with SDL's 8x8 debug font — legible, but
it looked like a debug overlay.

With **SDL3_ttf** present the port uses a real system font instead, anti-aliased and
blended against whatever the game already drew. It is an *optional* dependency: without it
the debug-font path still runs, so a build with nothing but SDL3 keeps working. CMake says
which it picked at configure time.

No font is shipped — this repository carries no binary assets, and shipping one means
shipping its licence. A system font is found at runtime from a short candidate list
(DejaVu Sans, Liberation Sans, Noto Sans, and the usual macOS paths). `LF2_FONT=/path/to.ttf`
overrides it. If nothing is found the runtime says so on stderr rather than quietly looking
worse than it should.

**What this does and does not cover.** GDI text is the menu's copyright block and the whole
character-select panel — player numbers, `Computer`, `Join?`, fighter names, team, music and
difficulty labels. It is **not** the in-match text (`VS mode (Difficult)`, the `1`/`Com`
name tags) or headings like `Character Selection`.

### The other font, and what porting it would take

Those are drawn from the game's own **8x16 fixed-pitch bitmap sheet**, one blit per glyph —
`LF2_BLT_RECTS` during a match shows `VS mode (Difficult)` as twenty consecutive 8-pixel
destination rectangles stepping across `y 532..548`. Scoped, not done:

| | |
|---|---|
| `fn_00423940` | draws a string, looping glyphs at 8 px pitch — the port target |
| `fn_00423a70` | calls it four times at ±1 px offsets, which is the outline |
| `fn_0041b130` | builds `"VS mode " + "(Difficult)"` and right-aligns it as `0x316 - len*8` |

Three things make it more than a typeface swap, and they are why it stopped here rather
than being half-done:

- **Layout assumes 8 px per character.** Right-aligned text positions itself by
  `x = right - len*8`, so a proportional face lands short of where the game intended unless
  the port re-derives the alignment.
- **The sheet contains CJK**, so a replacement needs a font with the same coverage, which
  is not something that can be assumed present on a user's machine the way DejaVu Sans can.
- **The argument semantics are not pinned down.** Reading constants at the call sites gives
  `(str, x, y, ...)` but the fourth argument reads as `0x40` at one site and `0x01` at
  another, and a first pass at tabulating them was wrong because register-pushed arguments
  do not show up in a scan for constants. Porting on that reading would be guessing.

## Window mode

`LF2_WINDOW` selects how the window is created. The game itself only ever asks for a
fixed-size bordered window, so this is the port's choice rather than something the game
can express.

| Value | Effect |
|---|---|
| `windowed` (default) | resizable bordered window at the game's native 794x550 |
| `borderless` | same size, no frame |
| `fullscreen` | borderless fullscreen |

**Alt+Enter** toggles fullscreen at any time.

### The window decides how wide the game is

Making the window wider gives a genuinely **wider field of view** — more world, not the same
picture with bigger pixels — and it happens while the game is running, on the frame after the
resize. This used to be `LF2_WIDESCREEN=<w>`, read once at startup, which is a developer's
escape hatch rather than a feature (issue #20).

The window drives **two** numbers, and keeping them apart is the whole of it (issue #41):

- **SCALE** — `min(win_h/550, win_w/794)` — how many screen pixels a game pixel becomes. The
  picture **fills the window**.
- **COMPOSITION** — `win_w / scale`, floored at 794 — how much **world** is on screen. The
  game's own width words are patched to it (`wide_apply` in `runtime/overrides/menu.c`), so
  this is a real field of view: the camera and the layer loops draw more world.

**The height sets the scale, and that is not arbitrary.** LF2's vertical screen axis carries
the depth (z, bounded by `bg.dat`'s `zboundary:`) and the jump height, both fixed by the
stage's own data, and every layer's picture is 550 rows tall. Composing 1080 rows was tried:
the game draws its world in the top 550 and the remaining 530 are black, because there is
nothing behind them to draw. There is no more world *vertically* at any window size — so the
rows that exist are scaled to the rows the window has. Horizontally there **is** more world, so
leftover width is spent on field of view instead of magnification.

At the game's own 794x550 the scale is exactly 1 and the composition is exactly 794, so
nothing about the 4:3 game moves. Every byte-identity arm in the suite depends on that.

**The scale is applied PER QUAD, not to the finished frame, and that distinction is the
feature.** The port used to compose `550 * 1920/1080 = 978` pixels of world into a small buffer
and let SDL blow the whole picture up by 1.96 — not an integer, so a game pixel landed as a
block two **or** three screen pixels wide, and text and lighting were quantised to the small
grid before being enlarged. It then swung the other way and drew everything 1:1 with 265 rows
of black above and below a 1080p window. Now the native renderer scales each quad in the
display list as it draws into a full-resolution target: the geometry is exact at float
precision and only a sprite's own texels are magnified, once, `NEAREST`, from the source art.
At 16:9 the composition width comes out at the same 978 the old aspect rule gave — the numbers
coincide, the designs do not.

The **software compositor cannot do this** and does not pretend to: by the time a frame reaches
it every sprite has been flattened into one buffer, so it stretches that buffer to the same
rectangle. It is the fallback (`LF2_RENDERER=soft`, or no SPIR-V backend) and it looks like one.

A window narrower than 794 still gets 794 and is cropped at the sides, because the HUD strip
is 792 wide and there is nothing sensible below that.

**Each fixed-794 screen is framed its own way** (issue #44), because they are not all
composed alike and the binary says so:

| Screen | Identified by | Framing |
|---|---|---|
| front end | its full-screen fill `0x10206c` | **left**-aligned |
| mode menu | its full-screen fill `0x122565` | **left**-aligned |
| loading | a whole-screen **picture** (`MENU_WAIT`), no fill of its own | centred, side bands extended from its own edge columns |
| character selection | its full-screen fill `0x000000` | centred (unchanged) |

The two menus are left-aligned because their character portrait (`MENU_BACK<n>`, the same
sprite on both screens) is drawn at a **hard literal x = 0** and hangs on the screen's left
edge — centring moved the anchor into the middle of the window. That is fidelity to the
author's framing, not a preference.

A screen is recognised by **what it paints**, not by a `.data` word: the two menu fill colours
each appear exactly once in the whole binary. That choice is deliberate — the word the port
used to treat as the mode menu's cursor is the game *mode* and reads 1/4/5 during a match
(issue #51), which is the same disguise the pre-fight overlay wore once.

The loading screen's side bands are **extrapolated** from its own edge columns, and the run
says so in as many words (`LF2_FRAMING_DEBUG=1` prints "a declared port choice, not the
game's"). Its backdrop is artwork, so unlike a flat colour there is nothing authored to put
beside it — extending the edge is the port's choice and is labelled as one rather than passed
off as the game's.

**A fixed-width screen is centred, but its BACKGROUND is not** (issue #42). The front end, the
mode menu, character selection and the pre-fight overlay are all fixed 794-wide screens, so on a
wider composition they are centred — and the centring is applied *while composing*, to the draws
that fit inside the game's own 794-wide screen, not to the single copy that carries the
composition to the primary. That distinction is what lets a screen's own full-screen colour fill
be treated as the **background**: it spans the composition and starts at the left edge, with the
screen's artwork centred on top of it. A very wide, short window used to show the front end
jammed against the right with black down the left, because the backdrop was widened and then the
whole composition was shifted right over it.

Only a flat *colour* fill is extended this way. Character selection and the overlay have artwork
for a backdrop, and stretching a picture sideways invents layout the game does not have — the
same answer issue #23 gives for a stage's sky — so those still have black beside them.

Because the copy to the primary is now 1:1, every column of it is written every frame, which
removes issue #29's ghost by construction rather than by clearing it.

**The wider view is CENTRED on what the 4:3 view showed**, not extended to the right. The game
puts the players' centroid in the middle of a 794-wide window and says so in one instruction
(`SUB ESI,0x18d` at `0x0041bb7d`, where 0x18d is 794/2), so a wider view left the centroid 397
px from the left edge and bolted the extra picture onto one side. The world is now *drawn* from
a camera shifted left by half the extra width.

That is a **draw-time** value, and the reason matters: `fn_0041b5d0` eases the camera toward
its target by a seventh and reads back whatever is in the camera word, so subtracting the
offset there each frame has fixed point `target - 7*offset` — the view ends up seven times
further off than asked for, and drifts there gradually, which reads as a wandering camera
rather than a wrong constant. The game's camera is left exactly as the game computed it; only
the parallax and the object pass see the shifted one.

It is clamped at the stage's left edge, so it only applies where there is world to move into.
**`LF2_CAMERA=1`** reports whether it fired *and why not when it didn't* — a run can be
perfectly correct and shift nothing:

| Window | Offset | What happens |
|---|---|---|
| 794x550 | 0 | nothing re-centred, by definition |
| 1100x550 | 153 | all frames re-centred — camera 400 draws as 247 |
| 1920x1080 | 563 | nothing re-centred: Brokeback Clif is 1500 wide, the whole stage already fits, the camera never leaves 0 and there is no world to centre into |

**Stage mode's section lock gets the same substitution.** `fn_0041b5d0` bounds the camera a
second time by `[0x00450bb0]` when that is non-zero — the thing that holds the camera partway
along a stage until the section is cleared — and says it, like everything else, against a
794-wide screen. It is now bounded by `lock + 794 - view`, so the camera stops the same
distance from the walk boundary at any window width. `LF2_CAMERA=1` reports whether the lock
was set *and whether it actually bound the camera*, which are different things: the
substitution only does work when the lock is the tighter of the two bounds.

A note for anyone looking for these constants: a grep for `794` (`0x31a`) finds 34 sites and
**none of them are the camera**. The binary writes it as `-794` = `0xfffffce6` inside an `ADD`
or a `LEA`, and there are exactly three such sites — `0x0041bba2` and `0x0041bc54` in the
camera, and `0x004377d1`, a walk-boundary test against a player's x.

**Audio is no longer culled on the right.** The game pans a sound between two speakers placed
on the *screen* at x 200 and x 600, each reaching 400 px — the 794 screen's quarter points,
written down as pixels (`runtime/overrides/audio_pan.c`). That gives an audible span of
-200..1000, wider than the game's own picture, which is why nothing is ever culled at 794 and
why the function had never been looked at. Widen the view and the span does not move: at 1920
everything past screen x 1000 had a volume of exactly zero — the right 48% of the picture,
silent. The constants are now scaled by `view/794`, which is a scale rather than a
re-derivation on purpose: `view/4` would give 198 at the native width instead of the 200 that
shipped. **`LF2_AUDIO_PAN=1`** prints the audible span against the picture's width, and
`ctest geometry` asserts the speakers are at *exactly* 200 and 600 at 794, walks every
on-screen x at 794 and at 1920 finding none silent, and — the arm that gives the others
meaning — shows the unscaled constants silencing a 1920 picture from x 999. That used to be
a three-run, 270-second script; it is arithmetic, it lives in `runtime/overrides/geom.h`
alongside the code that ships it, and it now runs in a millisecond.
**`LF2_AUDIO_PAN_RAW=1`** still turns the scaling off in a real run.

| Window | Scale | World on screen | On screen |
|---|---|---|---|
| 794x550 | 1.000 | 794x550 | the game's own picture, untouched |
| 1600x550 | 1.000 | 1600x550 | fills it — all the extra width is world |
| 1920x1080 | 1.964 | 978x550 | fills it — the extra height is scale |
| 800x900 | 1.008 | 794x550 | the **width** binds; leftover rows are black, correctly |

`tools/e2e.sh widescreen` asserts exactly that table, both columns. The 794 row is the one that
must NOT widen or scale — without it a build that always widened would pass everything else.
The 800x900 row is the negative for "the picture fills the window": that window is taller in
aspect than the game, so a band is the right answer, and a build that stretched everything
unconditionally fails there and nowhere else.

**There is no separate object magnification any more.** There used to be: objects were scaled
by a whole number about their own base while the stage stayed at 1:1, which put a 2x fighter on
a 1x floor and made the frame disagree with itself about how big a pixel was. The world scale
above subsumes it — everything in the display list gets the same number, so an actor's size and
its position come from one place. The trap that cost a rewrite is worth keeping in mind because
its shape is still available: anchoring a size change anywhere but the sprite's own rectangle.
Scaling an object about its **ground marker** grew the height of its jump by the same factor it
grew the fighter, and a launched object already 314 px up went to 628 and off the top of the
screen. A uniform scale cannot do that, because the floor moves with it.

**`LF2_MODE=<name>`** chooses which of the game's eight modes a run enters: `vs`, `stage`,
`championship1`, `championship2`, `battle`, `demo`, `playback`, `quit`. It presses nothing — it
puts the *game's own* mode-menu selection where the run asked and lets the route's existing
confirm dispatch it, so the mode change, its sound and its screen transition are all the
game's. The hold is released once the pre-fight overlay is up, so it cannot fight a later
screen, and an unknown name is refused loudly rather than silently entering VS.

This is what makes anything but VS mode testable. Every scripted route used to reach the game
by pressing buttons at counted frames and take whatever the mode menu happened to be sitting
on, so one of eight modes was exercised and seven were not — and the stage-mode camera lock
had no way to be verified at all. `tools/e2e.sh stage_mode` uses it, and its report says explicitly
when the mode menu was never reached, because a route that silently entered VS while asking
for stage would be a green test for a mode it never visited.

**`LF2_WINDOW_SIZE=<w>x<h>`** sets the window's initial size. It is not the old knob renamed
— it names a window, and the composition is derived from it by the same code a window manager
drives when someone drags an edge. It exists because a headless run
(`SDL_VIDEODRIVER=offscreen`) has nobody to drag one.

Two things make this affordable at runtime. The surfaces that follow the window are allocated
**once** at the maximum width with their **pitch fixed** there, and a resize only moves
`s->w`: `vram_alloc` is a bump allocator with no free, so reallocating per resize event would
exhaust the arena during a single drag. And `Lock` reports width, height and pitch fresh on
every call, so the game picks up a changed width on its next frame while anything it cached
stays valid.

## Decompiling a function with Ghidra

`re/instructions.tsv` is a disassembly: it answers *what bytes are here*, not *what does this
function do*. When the question is a constant or a piece of layout, decompile instead — reading
2000 bytes of x86 to find four numbers is how a wrong constant gets copied into the port.

```sh
printf '00429730\n' > scratch/decomp_targets.txt
LF2_DECOMP_TARGETS=scratch/decomp_targets.txt \
analyzeHeadless scratch/ghidra lf2 -process lf2.exe -noanalysis \
  -scriptPath tools/re/ghidra_scripts -postScript DecompDump.py
# -> scratch/decomp/00429730.c
```

The project lives in the gitignored `scratch/ghidra/`, so the slow auto-analysis is paid for
once; drop `-noanalysis` (and `-import game/lf2.exe`) the first time. `DecompDump.py` prints a
line per target **including the misses** — a VA in no function, or one the decompiler refuses,
says so rather than leaving a gap in the output directory that reads like a file nobody opened.

This is how the pre-fight overlay's row geometry was settled (claim C022). The port had a
uniform 24-px step measured off three sampled highlight blits; the decompile shows six literal
draw calls at y 16, 39, 64, 87, 111, 137. The three rows that had been sampled are exactly the
three a uniform step gets nearly right, so no amount of re-measuring by that method would have
found it.

## The pause menu

The game has no pause; this is the port's, built on declining to call the game's own
per-frame update. **Escape** or **Start** during a match opens it, and it takes mouse,
keyboard and pad like every other menu here.

| Item | |
|---|---|
| RESUME | |
| DROP OUT | only when the device that OPENED the menu is driving a player this port put into the match — a drop-in. It runs the same `coop_leave` an unplugged pad does, which refuses any slot the game's own character selection filled |
| LEAVE MATCH | drives the game's own way out of a fight — F4, then the pre-fight overlay's own Exit item — and lands on the character-select screen with the roster cleared |
| OPTIONS | the light's direction: **LIGHT ANGLE** (which way it comes from) and **LIGHT HEIGHT** (how high it is), in degrees. Left/right adjust in 5° steps, confirm nudges, BACK returns. Both feed the *one* light vector, so the shading on the fighters, the direction their shadows point and how long those shadows are all move together — and the frame is frozen while you do it, so you watch them move |
| QUIT GAME | ends the process |

Two things about it are worth knowing before changing it.

**Drop-out is per player, so the menu records which device opened it.** Escape means the
keyboard, Start means the pad holding it. A menu that guessed would take the wrong fighter
out of the fight.

**Both DROP OUT and LEAVE MATCH unpause first.** Pausing works by not calling the game's
update, so anything the *game* has to do — and leaving a match is one of those — would
otherwise be delivered to a game that never runs another frame.

`LEAVE MATCH` is named for what it verifiably does. Reaching the front-end menu from the
character-select screen is one further step that is **not** established: Escape there does
nothing, measured, and issue #22 stays open for it. The port does not fake the transition by
resetting its own state.

`tools/e2e.sh pause_dropout` covers the drop-out half end to end, including the negative that
player one is still in the match afterwards.

**RmlUi was considered for the Options page and declined**, and the reasoning sits beside the
code in `runtime/app/pause.c`. Dusklight uses RmlUi for its game-facing UI and is right to — it
has documents, components and a whole settings tree. This is two numbers on a menu that
already exists, already takes keyboard, pad and mouse, and is already drawn with the game's
own glyphs so it looks like the game. RmlUi is C++ with its own build, font stack and render
backend, and would become the largest dependency in a port whose entire build is a C compiler
and SDL. If a real settings screen ever lands that judgement should be revisited.

The setting does **not** persist across runs: there is no config file in this port yet, and
inventing one for two numbers is the wrong order to do things in.

## Scripted input

For checking the port without a human at the keyboard:

**Prefer the screen-keyed form.** `button@<screen>[+<n>]` fires *n* frames after the game
first DRAWS that screen — `charselect`, `overlay` or `match` — so a press aimed at the match
lands in the match however long the ~13 s data load took. Frame numbers are exact and
reproducible within a run, but the frame a screen *arrives* on is not: it moves with the load
and with how busy the machine is, and every regression test's route was once a stopwatch aimed
at a moving target (issue #18, which went red three times for that reason and never for a real
one). Every route in `tools/` is screen-keyed end to end, the opening press included.

**The opening press is `@frontend`, and it is where the wall clock went.** Every route used to
open with `south:900` — a frame number picked to be safely past a load that had not started
yet. The front end is in fact drawn and taking input on **frame 1**, and the mode menu follows
six frames after the first press whenever that press lands. Anchoring it cut **840 frames off
the front of every run** (issue #57).

A press whose screen never appears **never fires**, and the run says so at exit along with the
screens that did appear — silently not pressing is how a route that missed its screen reads as
a feature that did not work.

Every run prints its denominator, whether or not anything went wrong:

```
scripted input: screens reached -- charselect@906 overlay@1746 match@1968
LF2_VIRTUAL_PAD: 21 of 21 items fired
LF2_CLICK_SCRIPT: item 0 `403,228:900' NEVER FIRED -- its screen never appeared, …
```

The `N of M` line is the point: a clean run has to be distinguishable from a report that was
never reached. It is printed per script, so each of the four gets its own line. Presses that did
not fire are named with their own text out of the script, including a button name this build
does not know — a typo used to be skipped in silence.

This report was wrong until issue #24 and should not be trusted in output from before it: a
single sticky flag conflated "cannot fire *yet*, its screen has not appeared" — true of every
screen-keyed press on frame 0 — with "never fired", so **every** screen-keyed route ended with
the warning, clean ones included.

**All three devices take both forms.** They used to differ — the `@<screen>` form existed only
for the pad, and the keyboard and mouse parsed a bare number — which is why four route tests
were still stopwatches (issue #25). The timing model, the screen signal and the exit report
now live in `runtime/app/script.c` and are shared.

| Variable | Effect |
|---|---|
| `LF2_KEY_SCRIPT="<vk>@<screen>[+<n>][,...]"` | press that key *n* frames after that screen is first drawn, held 8 frames |
| `LF2_KEY_SCRIPT="<vk>:<frame>[,...]"` | the same, keyed to a presented frame |
| `LF2_CLICK_SCRIPT="<x>,<y>@<screen>[+<n>][;...]"` | place the pointer and click, keyed to a screen |
| `LF2_CLICK_SCRIPT="<x>,<y>:<frame>[;...]"` | the same, keyed to a presented frame |
| `LF2_VIRTUAL_PAD="<button>@<screen>[+<n>][,...]"` | the controller equivalent, keyed to a screen |
| `LF2_VIRTUAL_PAD="<button>:<frame>[,...]"` | the same, keyed to a presented frame |
| `LF2_WINDOW_RESIZE="<frame>:<w>x<h>[,...]"` | resize the window on that frame — a stand-in for a window manager |
| `LF2_FRAME_DUMP="<frame>\|@<screen>[+<n>][,...]"` | dump those presented frames; **screen anchors work here too** |

Screens are `frontend`, `charselect`, `overlay` and `match`. **`frontend` is the game's first
screen**, identified by the flat backdrop colour that only it paints (`0x10206c`, pushed at
exactly one site in `.text`), so it cannot be true on a run that never got there. **`charselect`
is the post-load panel, and
that panel is also the mode menu** — the two share a blit destination, so the signal goes up
when the mode menu appears, a little before character selection proper. It is a reliable
reference point (it is the first thing after the load, and the load is the part that moves),
but it does not mean character selection is on screen, and a route that needs to tell the two
apart has to count frames from it.

**`@match` means the HUD is up, NOT that fighters are on screen.** The stage load happens after
the overlay's confirm, and the HUD strip is drawn across it, so the gap between `@match` and
the first fighter being drawn is a load and does not hold still: on the fast routes
`controller_test` saw 73 keyed blits at `@match+691` and 71988 by `@match+1131`. A route that
asserts on gameplay needs margin after `@match` rather than a tight offset, and its threshold
should be checked against the run's own counters, not assumed to scale.

`LF2_WINDOW_RESIZE` exists because the headline of the widescreen work is that the field of
view changes *while the game is running*, and no scripted run can produce that on its own: a
headless run has no window manager, so SDL never delivers a resize. It drives the same entry
point the real event does — the only thing it stands in for is the drag. A step whose frame
the run never reaches says `NEVER FIRED` at exit rather than leaving the run looking as
though it resized.

The older wall-clock form is still there for probing (`LF2_AUTOKEY`, `LF2_AUTOKEY_START`,
`LF2_AUTOKEY_EVERY`, `LF2_AUTOCLICK` and their `_ONCE` variants), but nothing that needs to
land on a particular screen should use it.

The pointer is placed four frames before the button goes down, because the menu hit-tests
where the pointer *is* when the click arrives — moving and clicking on the same frame races
the game's own read.

The port routes all input through ONE keyboard layout (see "One keyboard, first come
first served" below): arrows move, `Z` attacks, `X` jumps, `C` defends — so scripted keys
use `0x26`/`0x28`/`0x25`/`0x27` for the arrows and `0x5A`/`0x58`/`0x43` for the buttons.
(Historically the menus were driven by the player-1 controls from `data/control.txt`,
whose defaults were the numpad; the investigation notes below still reference those keys.)

### Reaching a match, deterministically

Both `tools/routes/smoke_test.sh` (mouse and keyboard) and `tools/routes/controller_test.sh` (pad only)
now play a VS match every run. The part that used to be luck was the pre-fight overlay —
Fight! / Reset All / Reset Random / Background / Difficulty / Exit — where a blind press
landed on whatever was selected, usually Reset Random, which re-rolls the characters and
stays put.

Its selection index is **`0x0044d06c`**, 0..5 top to bottom, up decrements and wraps, and
it is **2 on entry** (measured from boot, not assumed). So two ups reach Fight!.

Finding it is worth recording as a method, because reading the disassembly did not work:
searching `fn_0041bc90` for the compare returned twenty-odd candidates that could not be
told apart on sight. Instead `LF2_MEM_DUMP=<frame>[,...]` writes the whole `.data` section
and `tools/re/diff_data.py` compares two dumps across a single press:

```sh
LF2_MEM_DUMP=2290,2450 LF2_VIRTUAL_PAD="...,up:2350" ./lf2 lf2.exe
tools/re/diff_data.py scratch/data_002290.bin scratch/data_002450.bin --max 8
# 12745 dwords compared, 9 differed, 2 after filters
# 0044d06c  2 -> 1
```

Two candidates out of 12,745 dwords, and the right one confirmed by matching that 2 → 1
against the frame where the highlight moved Reset Random → Reset All. `diff_data.py`
reports the denominator at every stage on purpose — "no candidates" from a run that
compared nothing looks identical to "no candidates" from a run that compared everything.


Mouse input **is confirmed to reach the game**, established by logging what the window
procedure is actually handed (`LF2_MSG_DEBUG`) rather than inferring from the screen:

```
dispatch msg=0200 wparam=00000000 lparam=00e801ae wndproc=0043b3d0
```

`0x01ae`/`0x00e8` is x=430, y=232 — exactly what was sent. Over a run the procedure
receives `WM_MOUSEMOVE`, `WM_LBUTTONDOWN` and `WM_LBUTTONUP` along with the startup
`WM_MOVE` and `WM_ACTIVATEAPP`.

(An earlier note here claimed this was unconfirmed, after a calibration failed to show the
cursor moving. That was the wrong conclusion from the wrong instrument — the screen was
never the place to look.)

Delivery is confirmed all the way into the game's own state, not just to the window
procedure. The handler at `0043b8af` stores the pointer in two globals — x at `004546f0`,
y at `00453cdc` — and watching the first shows it take the value `0x1ae` (430), exactly
what was sent.

(An earlier note here concluded "the mouse is probably not how menu items are selected",
from seeing every hit-test against `004546f0` compare x against 590–725 rather than the
menu text at x 300–500. That was reading one hit-test — the advertisement panel on the
right — and generalising from it. The menu is entirely mouse-driven; its own item bands
are x 260–547, and the port now drives them.)

The game **never calls `GetKeyState`** — polling it produced no hits at all — so keyboard
input must travel entirely as `WM_KEYDOWN`/`WM_KEYUP` messages.

Scripted keys now count pumps rather than milliseconds (a wall-clock window is missed
whenever no pump lands inside it) and the transitions demonstrably fire:

```
autokey vk=65 down (pump 60)
autokey vk=65 up   (pump 68)
```

`PeekMessageA` was ignoring its remove flag. `PM_NOREMOVE` (0) means peek *without*
consuming, and always removing meant a peek silently ate messages a later `GetMessage`
should have returned. Fixing that made the whole startup sequence arrive — `WM_SIZE`,
`WM_ACTIVATE` and `WM_SHOWWINDOW` had all been disappearing — and key messages now reach
the window procedure.

**The menu still does not respond.** The game peeks only with `PM_NOREMOVE` — logging the
flag shows it never once passes `PM_REMOVE` — so it relies entirely on `GetMessage` to
consume, the usual "peek to test, get to fetch" loop. The port implements that contract
now, yet the same `WM_KEYDOWN` is dispatched around 2400 times in a short run, so
something in the peek/get interaction still redelivers rather than consumes.

The queue itself is provably correct. Logging peeks against gets (`LF2_QUEUE_DEBUG`) shows
clean alternation — each message peeked once without removal, then fetched once and
removed, with the ring advancing properly:

```
PEEK msg=0100 remove=0 ring=[0,1)
GET  msg=0100 remove=1 ring=[0,1)
PEEK msg=0101 remove=0 ring=[1,2)
GET  msg=0101 remove=1 ring=[1,2)
```

The ~2400 figure was never redelivery: `hostwin_pump` runs on every peek as well as every
get, and the game peeks thousands of times a second, so the pump-counted key was cycling
about a hundred times a second. Scripted keys are back on a wall clock and now produce
about 7 press/release pairs in 14 seconds.

**The menu still does not respond**, and the delivery side is now fully accounted for.

Following `WM_KEYDOWN` through the window procedure: it falls in the `0x20 < msg < 0x200`
branch to `0043b4fd`, which indexes a second jump table at `0043b512` covering messages
`0x100`–`0x112`. The entry for `WM_KEYDOWN` resolves to the handler at `0043b531`, whose
first act is a gate:

```
0043b531  MOV EBP,0x1
0043b536  CMP dword ptr [0x00458440],EBP   ; input enabled?
0043b53c  JNZ 0x0043b549                   ; if not, skip the key handling
```

Watching `00458440` shows it set to 1 early in the run by `fn_00401250`, so the gate is
open and the handler is reached — the game is not in a disabled-input mode.

Past the gate the handler calls `004031d0` with the virtual-key code, on the object at
`00458440`. Watching that call shows the key arriving:

```
enter 004031d0 from 0043b549  ecx=00458440  args: 00000065 ...
```

`0x65` is exactly the key sent, and the call count matches the number of presses. **So the
key is delivered all the way into the game's own input object.** Every stage of the input
path is now verified by measurement rather than inference.

The menu still does not respond, and a sweep of plausible keys — arrow down, Enter, numpad
2/5/0, space, F2 — changes nothing.

Two candidate consumers have been examined and neither is the menu:

- **The key queue at `00458440` is never read.** Only four instructions reference it: the
  enable check, the push from the window procedure, the write that enables it, and a
  10-byte constructor thunk. Nothing drains it, so it is most likely the buffer behind
  LF2's replay recording rather than live input.
- **The rectangle hit-test at `00424064`** tests both mouse axes against arrays at
  `00453c08` (y), `00453f70` (width) and `00453ce0` (height). Dumping them gives
  y=422, w=132, h=32 — which is exactly the advertisement layout in `data/ad0.txt`
  (`ba 0 422 132 32`). That code drives the ad strip, not the menu.

- **The game keeps its own key-state array at `00455378`**, indexed by virtual-key code.
  `fn_0043bf10` fills it with `0x75` at startup and the `WM_KEYDOWN` handler writes `0x64`
  at `0043b557` (`MOV byte ptr [EBX + 0x455378], DL`, with EBX holding wParam). This is
  what the game polls instead of `GetKeyState`, which explains why `GetKeyState` is never
  called. The chain right after it comparing wParam against `0x4c`, `0x46` and `0x32` is
  the "LF2" cheat-code detector.

  **The store works.** Probing `0043b557` shows it executing once per keypress with
  `ebx=0x65` (the virtual key) and `edx=0x64` (the value), writing to `004553dd` exactly as
  the disassembly says. An earlier note here called it broken because a memory watch never
  saw `0x64` — but the game clears the array every frame, and a watch that only reports
  differences between samples cannot see a value that goes `0x75` to `0x64` and back
  between two of them. The instrument was wrong, not the port.

So the input path is verified at every layer: SDL event, message queue, `PeekMessage`/
`GetMessage`, `DispatchMessage`, two jump tables, the enable gate, and finally the game's
own key-state array. Whatever keeps the VS-mode overlay up is past all of that.

- **The `WM_KEYDOWN` route is also text entry.** `004031d0`, the function the handler passes the
  key to, accepts space, period, letters and digits and appends them to a string buffer.
  It is for typing a name, not for menu navigation — which is consistent with nothing ever
  draining the queue.

**Resolved: the menu is mouse-driven, and the port navigates it.** The hit-test is at
`00427df5` inside `fn_004246b0`:

```
MOV ECX,[004546f0]      ; mouse x
CMP ECX,0x104 / 0x223   ; x within 260..547
MOV ECX,[00453cdc]      ; mouse y
CMP ECX,0x112 / 0x12c   ; y 274..300  -> entry 1
CMP ECX,0x131 / 0x14a   ; y 305..330  -> entry 2
```

Clicking inside a real band switches screens — `LF2_AUTOCLICK=400,287` reaches the control
settings page, which renders correctly with its four keyboard panels, control labels and
buttons.

Earlier attempts clicked at y=232, which is above the first band entirely, so nothing
happened. Coordinates estimated from a screenshot were never going to work; the game's own
comparison constants were the answer.

`LF2_DUMP_MEM` and `LF2_FIND_BLT` were added for this kind of question — the latter reports
which guest address issued a blit with a given destination rectangle, which is how
`fn_004246b0` was identified as the menu's renderer.

## Debug switches

All are environment variables read at run time, and all are off by default.

| Variable | Effect |
|---|---|
| `LF2_COM_TRACE` | log every COM method call, in the form the trace harness compares |
| `LF2_CLOCK_SITES` | every guest call site that reads the clock, with its total reads and its longest run of reads with **no Sleep between them** — the second is what separates a deadline loop from a spin |
| `LF2_RSRC_DEBUG` | log bitmap loads, `StretchBlt` calls and resource lookups |
| `LF2_STR_DEBUG` | log `sprintf`/`fscanf`/`fopen` with their formats and results |
| `LF2_WATCH=<hex>` | report writes to a guest address; `LF2_WATCH_VAL` filters to one value |
| `LF2_FN_WATCH=<hex>` | on entry to that guest function, dump its caller, `ECX` and arguments |
| `LF2_WATCH_REL=<off>` | arm the memory watch on a slot in that function's frame |
| `LF2_ESP_LOG` | log ESP either side of every host call once the watched function is entered |
| `LF2_DUMP_SURF` | write each surface to `scratch/surf_NN.ppm` after a GDI blit |
| `LF2_COM_TRACE` + `tools/re/diff_trace.py` | compare the call sequence against a Wine oracle capture |

Two are build options rather than environment variables, because they need code emitted
into the generated file:

```sh
cmake -S . -B scratch/build -DLF2_STACK_CHECK=ON   # assert stack balance at every guest RET
LF2_PROBE=4246fd,4274da cmake --build scratch/build --target lf2   # log ESP at those instructions
```

`LF2_PROBE` is read when the code is **generated**, not when it runs, so the target has to
be rebuilt after changing it.

## How far the game runs

Verified by scripted input and screenshots:

| Screen | Reached |
|---|---|
| Title menu | yes |
| Control settings | yes, renders fully |
| Mode selection (VS, Stage, championships, Battle, Demo, Playback) | yes |
| VS mode control overlay | yes |
| Character selection | yes — players join, characters assigned, all fields populated |
| A running match | **not yet** |

Starting a match needs the pre-fight menu (Fight! / Reset All / Reset Random / Background /
Difficulty / Exit) driven to its first entry and confirmed. Several approaches have failed
in ways worth recording:

- Attack with the default highlight on **Reset Random** re-rolls the characters.
- Pressing **up** to move the highlight instead cancels the join and empties every slot,
  with both player 1's numpad binding and player 3's arrow binding.
- Clicking directly on the Fight! text does nothing, so that menu is not mouse-driven.

Character selection itself works fully: joining, character assignment with portraits, and
every field populated — Player, Fighter, Team, Background, Difficulty and the music line.

**The menu keys are known.** Probing the consumer at `00419b73` shows it polling exactly
four virtual keys, continuously: `0x68`, `0x57`, `0x49`, `0x26` — Keypad-8, W, I and Up
arrow, which are the *up* bindings of players 1 to 4. So the pre-fight menu is navigated
with each player's own up key, and Up arrow drives it for player 3.

Pressing it does move the highlight, confirmed by watching it step from Reset Random to
Reset All. What defeats the scripted approach is that the number of Enter presses needed
to reach that menu varies from run to run, so a fixed cycling sequence lands its ups and
confirms in different places each time — sometimes re-rolling the characters, sometimes
cancelling the join.

Driving this wants either a human, or a script that reacts to what is on screen rather
than firing on a timer. The latter is the honest next step if it is worth automating.

That is a choreography problem rather than a port defect: every screen renders, and input
is verified all the way into the game's key-state array. It wants a human at the keyboard,
or a more careful script than a fixed cycling sequence.

## Following the game's input

`LF2_KEY_DEBUG=1` traces which virtual keys the game polls. The set it polls is a screen
signature — the title screen asks about very different keys than character select — so the
trace prints a set only when it *changes* from the previous sweep, giving a transition
timeline rather than one line per key.

**It will tell you it is blind, because on this game it is.** LF2 never calls
`GetKeyState`; it keeps its own key array at `0x455378`, filled from `WM_KEYDOWN`, and
reads that. The trace says so explicitly after 400 frames rather than printing nothing and
letting silence read as "no input". To actually follow input, probe reads of `0x455378`.

`LF2_KEY_DEBUG_SELFTEST=1` feeds the detector two synthetic sweeps, which must produce
exactly two `poll set changed` lines. Since the game never exercises this path, that
self-test is the only evidence the detector works at all — run it before trusting a
negative result from it.

## Watching guest memory reads

`LF2_READ_WATCH=<lo>:<hi>` reports which offsets inside a span the game loads. A malformed
or empty span is refused with exit 2 rather than silently watching nothing, and the hit
count is reported even when zero, so "saw nothing" is distinguishable from "was never
armed". Disabled it costs one predictable not-taken branch per load; frame throughput is
unchanged (~30 fps either way).

**Sequential scans are filtered out.** The game rebuilds its input bitmask by sweeping the
whole key array in order, so a raw read set is 250 sequential offsets on every screen and
says nothing. The scan is separable by *shape* rather than by call site: it is a long
strictly-ascending run, while a deliberate "is this key down" check is isolated and
out-of-sequence. Runs of `SCAN_RUN` (16) or more are dropped. This is why the tool does not
need the reading instruction's address, which would have meant tracking `eip` through the
generated code and paying for it on the hot path.

Sweeps close on the **frame**, not on a repeated offset. Closing on a repeat splits the
single array scan in two and leaves its tail (`7a 7b`) looking like a deliberate check —
a phantom finding, and exactly what the first version of this reported.

`LF2_READ_WATCH_SELFTEST=1` feeds a synthetic frame: a full 250-entry ascending scan plus
four isolated checks. It must report exactly `26 49 57 68`. Without it,
"(nothing but sequential scans)" would be indistinguishable from a filter that discards
everything.

### Raw mode: the per-byte profile

The scan filter above exists for one question — *which key does this screen check* — and is
exactly wrong for another. When the watched span is an array the game **sweeps**, the sweep
*is* the finding, and the filter throws it away.

`LF2_READ_WATCH_RAW=1` counts every read per byte and prints the profile on the same
300-frame boundary as the other periodic reports, resetting each time so a block covers one
window rather than the whole run. Zero counts are named explicitly (`+014..+41f  0`) rather
than omitted, because the absence is usually the result: a loop bounded by a count leaves
the tail of an array at zero and a full sweep with a per-entry test does not, and those are
different mechanisms.

Spans are capped at the 4096-byte window and a longer one is refused with exit 2 — reads
past the end would otherwise be dropped silently, which is the same profile as a game that
never made them.

`LF2_READ_WATCH_SELFTEST=1` in raw mode reads one byte four times and must report exactly
`+00c 4` with everything else zero.

This is what located the object gate in issue #15. Over the 400-entry object table, a match
frame reads entries 0..19 once each, 20..49 never and 50..62 heavily — so idle fighter slots
are visited every frame and skipped, and the loop is not bounded by a count. Over a single
*idle* object it reports exactly one hot byte out of 1056, `+0x338`, which turned out to be
a countdown the loop decrements for every entry rather than the gate. Both readings needed
the sweep the filtered mode hides.

### What it found, and what it did not

On every screen reachable so far — attract, main menu, control settings — the only
discriminating reads are `7a 7b` (F11/F12), and the set never changes. So **the key array
is not a screen signature on those screens**, and the idea of driving scripted input off it
does not work there.

It is not that the instrument is blind: the self-test shows it separates isolated checks
from a scan, and the pre-fight menu is known to check `68 57 49 26` (players 1-4 "up") at
`0x419b73`, which this would report. That screen simply cannot be reached yet. The blocker
is **navigation, not observation**: menus are mouse-driven and each screen's clickable
bands have to be recovered from the game's own comparison constants, the way the main
menu's were (see above).

## Starting a match

The port reaches gameplay. This runs a VS-mode fight, Louis against one computer
opponent, entirely unattended:

```sh
cd game && LF2_AUTOCLICK_ONCE=1 LF2_AUTOCLICK=403,228 LF2_AUTOCLICK_START=3000 \
  LF2_AUTOKEY_ONCE=1 LF2_AUTOKEY=0x5A,0x5A,0x5A,0x5A,0x5A,0x5A,0x5A,0x5A,0x26,0x26,0x5A \
  LF2_AUTOKEY_START=32000 LF2_AUTOKEY_EVERY=1800 \
  ../scratch/build/lf2 lf2.exe
```

The click picks **game start**; the game then loads its data for ~25 s, which is why the
key script starts at 32 s and why clicks and keys need separate schedules
(`LF2_AUTOCLICK_START`/`_EVERY` fall back to the `LF2_AUTOKEY_` ones). The eight attacks
walk mode select, player join and the computer-player count; the two ups move from
"Reset Random" to **Fight!** on the pre-fight overlay, and the last attack starts the
match. Both scripts need their `_ONCE` flag: cycling walks straight back out again.

## The object table, and adding a fighter to a running match

The state a fighter lives in, all located rather than guessed (issues #15 and #16):

| Address | What |
|---|---|
| `0x00458b00` | the game object, `this` for the input gather |
| `this+4` = `0x00458b04` | **one byte per object index** — an object is in the world iff its byte is 1 |
| `this+404` = `0x00458c94` | **400** object pointers, stride `0x420`; the player slots are the first eight |
| `this+2004` | the object-data registry: an array of block pointers, count at `+0x4d82000` |
| block `+1780` | the object's id from `data.txt` |
| block `+1784` | the object's **type** — 0 is a character |
| object `+872` (`+0x368`) | the object's data block |
| `fn_004064d0` | walks the table and applies the gate |
| `fn_004061d0` | `__thiscall` reset of one object record |

Every entry of the table holds a live pointer to a pre-allocated record from the moment the
data loads, so **being in the array is not being in the world** — the gate byte is. A
filled-in record without it is inert: it survives untouched for hundreds of frames, neither
updated nor reset.

| Variable | Effect |
|---|---|
| `LF2_COOP_TABLE=<frame>` or `live[+<n>]` | dump the table with each entry's gate byte. `live` fires off the game's own state |
| `LF2_COOP_REGISTRY=1` | with the above, dump the object-data registry and which block each live object uses |
| `LF2_COOP_PAIR=<i>,<j>` or `auto` | the dwords where two table entries differ |
| `LF2_COOP_SPAWN=<index>[,<id>[,<+0x364>]][;...]` | build object `<id>` (default 1, Bandit) at a free index and watch it. A **list**, so two spawns can be compared inside one run |
| `LF2_COOP_SHOT=<n>` | capture the frame `<n>` frames after the spawn |
| `LF2_COOP_REFS=<frame>` | scan the image, heap and stack for pointers to the player records |
| `LF2_COOP_JOIN=<slot>[,<id>]` | spawn into player slot 0..7 and set its selector and mask bit |
| `LF2_COOP_CHAR=<object id>` | pin where a late joiner's character cycle STARTS, so a test gets the same fighter every run. Not a switch — drop-in coop itself is always on |
| `LF2_COOP_TRACK=<index>` | that entry's position every 30 frames, while a match is on screen |
| `LF2_COOP_DEBUG=1` | the player slot table, printed on change |

**Prefer `live+<n>` to a frame number.** The data load does not take a fixed number of
frames, so a probe aimed at frame 2400 can land on character selection instead — and a
table dump taken there is 400 lines of untouched defaults, which reads exactly like a
result. The dump now says `NOT A MATCH` outright in that case.

```sh
cd game && LF2_COOP_TABLE=live+60 LF2_COOP_SPAWN=13,1 ../scratch/build/lf2 lf2.exe
```

puts a Bandit into a running match: it animates, walks, collides and gets its own HUD bar
with the **right portrait**. Spawning an id that is not in the registry is refused with the
number of entries searched.

**Compare two spawns inside one run, never across two.** VS mode randomises the characters
already on the stage, so a screenshot A/B across processes shows differences the changed
variable had nothing to do with — a first attempt at this had all three portraits differing
and one arm that never reached a match at all. `LF2_COOP_SPAWN` takes a list for that
reason, and `LF2_COOP_SHOT` keys the capture to the spawn rather than to a frame number,
because the data load does not take a fixed number of frames.

```sh
LF2_COOP_TABLE=live+60 LF2_COOP_SPAWN='13,1;14,52' LF2_COOP_SHOT=45 ./lf2 lf2.exe
```

draws two different characters in HUD positions 4 and 5; the same id twice draws the same
portrait in both. That two-sided pair is what shows the portrait follows the object's data
block at `+872`.

### Drop-in coop

Always on; there is nothing to switch on. A device that presses anything for the first time
while a match is already running claims a free player slot, and since character selection is
over, it chooses **in its own HUD panel**: the candidate's portrait and bars appear in that
player's empty box along the top of the screen and flash there, **left/right** cycle the
game's roster and **attack** (A on a pad, Z on the keyboard) locks it in. From the lock-in
the device drives it like any other player. Unplugging that device takes its fighter back
out again.

**The fighter is not on the stage until the lock-in** — it cannot walk, be hit, or be seen,
and the match carries on around a player who is still deciding. That takes some doing,
because the HUD strip (`fn_0041ae60`), the stage pass (`fn_0041a5a0`) and the world step
(`fn_004064d0`) all read the same per-slot byte, `this+4+i`: not building the fighter leaves
no panel to choose in, and building it puts a body in the middle of a fight (issue #19, which
is what shipped first). The two passes are made to disagree in the only place they can, which
is between them — `runtime/overrides/hud.c` raises the byte for a choosing slot, calls the
game's own panel drawing, and puts it back down. So the panel you choose in is the game's,
drawn by its code from its record; the port is not painting a character-select screen over
the match.

```sh
cd game && ../scratch/build/lf2 lf2.exe
```

The remaining `LF2_COOP_*` variables are the **diagnostics** over this — `_SPAWN`, `_JOIN`,
`_TABLE`, `_REGISTRY`, `_PAIR`, `_REFS`, `_TRACK`, `_DEBUG` — with one exception:
`LF2_COOP_CHAR` pins where the cycle starts, so a test gets the same fighter every run.

The slot it takes is the **lowest one with no fighter in it** — a second human is Player 2.
It used to prefer the lowest slot whose device *selector* was zero, on the reasoning that a
non-zero selector means character selection put a computer there. The reasoning is right and
the conclusion was wrong: that computer's fighter sits at its own high table index, unbound
by the gather, so the selector says nothing about whether slot 1 can hold a human — and what
it produced was a joiner in slot 4 with Player 2's box empty beside it (issue #21). Joining a
slot the roster listed as a computer does not replace that computer; the match gains a
fighter, which is what a drop-in is, and the run says so rather than doing it quietly.

The roster it cycles is **the game's own**: the
registry entries whose type field (`+1784`) says character, less the template at id 0. That
comes to 23, which is LF2's selectable roster. The type field was located against `data.txt`
— whose `<object>` section declares an id *and* a type per entry, and which the registry
reproduces in file order — by requiring a candidate offset to match the declared type on
**all 65** entries; `+1784` is the only offset in 2048 bytes that does. The alternative,
hard-coding the id ranges characters happen to occupy (1–11, 30–39, 50–52), would have
worked on this `data.txt` and broken on any mod that adds a character.

The pick is **deterministic**, from the frame the join lands on — a joiner wants a varied
character, not an unpredictable one, and a run you cannot reproduce is worse to debug than
one that always picks the same fighter. If the roster cannot be read the join is refused
with the reason rather than falling back to a hard-coded id.

It stays **opt-in** because a late joiner still gets no character-select screen; what they
get is chosen for them.

`tools/routes/coop_dropin_test.sh` (ctest `coop_dropin`) guards it two-sided — the same join with
and without a direction pressed afterwards. Both arms assert the join happened first,
because a run that never reached the match would otherwise pass the quiet assertion for the
wrong reason.

**It does not measure displacement, and that is deliberate.** A fighter that joins mid-fight
lands beside the brawl and gets knocked about: the idle arm drifted 56 px in one run and 69
in the next against ~120 for the driven arm, so no threshold separates them and the first
version of the test passed twice by luck. The claim splits in two instead, each half
measured where it is clean:

| Test | What it establishes |
|---|---|
| `coop_dropin` | the pad's input reaches the **joined fighter's record** |
| `two_human_match` | the game turns input in a player record into **movement** — on a fighter at its own start position, ~1350 px against 0 |

Together they cover "the pad drives the fighter it joined", and neither is confounded by the
fight moving things on its own.

The eight-player cap comes from the device-selector table's own size now, not a literal.
A fighter placed in slot 4 is drawn by the game's own name plate as "5".

**"Is a match on screen" is `panel_hud_up()`, not a gate byte.** Two home-made versions of
that test were wrong first. An object's gate byte goes up and down during *character
selection*, with the object still at the origin — so "something is in the world" admits the
join screen, and even excluding the character-select panel leaves a window before that panel
is first drawn. The HUD strip is the signal the widescreen code already relies on for the
same distinction, and the drop-in and `LF2_COOP_TRACK` both use it now.

### Two humans in a match

`tools/routes/two_human_match_test.sh` (ctest `two_human_match`) covers what `controller_2p` never
reached: `controller_2p` proves the second pad *joins at character selection* and quits at
frame 1900, so "a second human's fighter is actually driven once the fight starts" had no
coverage.

Getting there needs three presses from pad two, not one: character selection asks each
joined player for a **Fighter** and then a **Team**, and player one cannot proceed until
every joined player has finished. With only the join press the screen sits there with both
players joined and nothing happening — player two's rows drawn in cyan, still being chosen,
against player one's in white.

**Player slot `i` is object index `i`** — the game's own code, not this port's assumption.
`fn_00419a60__orig` walks `&devsel[0]` and `this+404` in lockstep, `EBP += 4; EAX += 4`,
looping while `EBP < 0x450b6c`. A human player's fighter must therefore be at its own index;
there is no other route by which the game could deliver its buttons.

A **computer's** fighter is not bound by that — its AI writes buttons straight into the
object it drives — so it can live anywhere, and one is observed at index 11. That is why the
joined mask can read two players while object index 1 is empty: the mask tracks the
character-select roster, in which a slot marked "Computer" counts as taken, not what
occupies the player object slots.

So **joining into a slot the game filled with a computer does not replace that computer.**
Its fighter stays on the stage at its own high index, and the match gains a fighter rather
than swapping one.

## The menu map, and how it was recovered

`403,228` is **game start**. Clicking it loads the game data (`Now Loading… data\*.dat`,
about 25 s) and reaches the mode menu — VS mode, Stage mode, Championships, Battle mode,
Demo, Playback, Quit. Player 1's keys then drive it: up is Keypad 8 (`0x68`) and attack is
Keypad 5 (`0x65`), which the control settings page states outright. From there it reaches
Character Selection and the Battle-mode team setup.

Main menu bands, from `tools/re/click_bands.py`:

| Item | Game y | Band |
|---|---|---|
| game start | 228 | above the extracted bands |
| network game | 259 | — |
| control settings | 292 | y 274..300 |
| recording info | 322 | y 305..330 |
| official website | 353 | y 336..361 |

All at x 260..547, so x=403.

**The mistake worth recording: I probed the three extracted bands for a long time without
ever screenshotting the menu.** The extractor only finds bands with an explicit
`x_lo,x_hi,y_lo,y_hi` comparison run, and "game start" is not hit-tested that way, so it
was missing from the table — and a table that looks complete reads as complete. One
screenshot showed five menu items where the extractor had found three. Look at the screen
before searching it.

### Tools

- `tools/re/click_bands.py` — recovers clickable bands from the game's own comparison
  constants. Only comparisons against the register the mouse coordinate was loaded into
  count; bounds stay in source order, since sorting destroys the lo/hi pairing. An x pair
  may be followed by several y pairs (one menu, several entries).
- `tools/re/find_path.py` — greedily extends a click path, keeping any candidate that yields
  one more screen transition.
- `LF2_SCREEN_HASH=1` — reports a screen change when a large fraction of a subsampled
  framebuffer signature differs, so menu animation does not register. This is the only
  usable "did anything happen" signal, since the key array reads the same on most screens.
- `LF2_AUTOCLICK_ONCE=1` — walk the click list once. Cycling walks back out of the menu,
  which looks like the game oscillating between two screens.

### The read-watch, validated against a prediction

On the character-selection screen `LF2_READ_WATCH=0x455378:0x455478` reports:

```
09 0d 10 11 20 25 26 27 28 41 44 49 4a 4b 4c 53 57 58 60 62 64 65 66 68 6b 70 ... f8 f9
```

That contains `68 57 49 26`, exactly the four VKs predicted from the polling site at
`0x419b73` (players 1-4 "up"), plus every binding the control settings page lists for all
four players. The instrument was built before this screen could be reached and its output
matches an independent prediction, which is the strongest evidence available that it works.

## Audio

Sound effects go through DirectSound on SDL3 and work with no setup.

**Background music needs `ffmpeg` on PATH.** The game's BGM is eight `.wma` files, and
rather than carry a WMA decoder the track is decoded to raw PCM by `ffmpeg` once, when the
DirectShow graph renders it. That keeps it an optional *runtime* dependency: without
ffmpeg you get silence and a message, never a broken build.

`LF2_AUDIO_DEBUG=1` reports the audio path as four independent counters, because they fail
independently — the game may never create a buffer, never start one, never have the device
pull, or pull and get silence:

```
audio: buffers=116 plays=1 device-pulls=1320 peak=31420/32767 music-frames=3325952
```

`peak` is the one that proves sound would actually be heard; the rest can all be non-zero
while the output is silent. `clipped` exists because a saturated peak cannot say how much
it saturated by — in a full match it is 15 samples in 4 million (0.0004%), i.e. inaudible,
where the peak alone reads as an alarming `32768/32767`.

**Report periodically, not once.** A single report early in a run lands before the match has
started and measures only the menus: it shows `plays=1` and reads as if effects never fire.
Across a match it goes to 8, which is the CPU opponent hitting an idle player.

Two things this turned up. The filename arrives as UTF-16 but with a **zero BSTR length
prefix**, so the terminator is what to trust, not the prefix. And the audio device was only
opened when a sound *effect* first played, so music alone — which is all that happens on
the menus — decoded a full track that nothing ever pulled.

ffmpeg is run with `fork`/`exec`, not `popen`: the path comes from the game's data files,
and interpolating it into a shell command string would make a filename a shell injection.
Quoting it would be a patch over the wrong mechanism — no shell needs to be involved.

The graph's `Stop` and `Pause` silence the music. Without that a track change layers the
new track over the old one, since the graph is the only thing that knows a track is
finished with.

`IBasicAudio::put_Volume` is honoured. The game sets **-500 centibels** (-5 dB) for music,
which is a gain of 0.562 — and the measured mix peak is 18426/32767 = 0.562, so the scale
conversion is right rather than merely plausible.

The decode is synchronous and measured at **0.15 s** for a 150-second track, so it needs no
threading or streaming. Both paths are verified: with ffmpeg the track loads and mixes;
with `PATH=/nonexistent` it prints `ffmpeg not found on PATH`, reports `music-frames=0` and
carries on silently.

## CPU use

The port sits at roughly **14% of one core** during play. It was 96% until `Sleep` was
implemented: the import was mapped to a no-op that returned immediately, so the game's
frame pacing — a `Sleep` in a loop — became a spin.

### The guest clock is the frame counter, and the host does the pacing

Guest time is exactly `presented frames × 33.33 ms` plus the sleeps the game took. It never
reads the wall, so how much of the game's timeline has passed by frame *N* is a property of
the game rather than of how fast or how busy the machine is — which is what makes every
frame-scheduled script mean the same thing everywhere (issue #18, claim C014). Real time then
comes from the **present**: each frame is held until the wall reaches its due time, so the
guest counts and the host paces.

Three details are load-bearing, each found by its own failure:

- **A `Sleep` is credited as a floor, `ms + 1`.** `Sleep(n)` returns after *at least* n, and
  `nanosleep` never returns early. Credit exactly *n* and the game's pacer lands on its own
  boundary and stops: it sleeps `remaining` when that is ≤ 5, arriving at `elapsed == 33`
  exactly — where `elapsed <= 33` sends it down the sleep path and `remaining == 0` sends it
  straight past the `Sleep`. Neither working nor waiting: 59 million clock reads at frame 0
  with no frame ever presented.
- **Sleeps are credited during play too**, not only on the load's fast path — the startup
  waits happen before the load is flagged and produce no frames. It costs nothing once frames
  flow, because 33.33 ms is above the 33 ms threshold the game compares against, so its loop
  always takes the overdue branch and stops sleeping.
- **The host pacer drops its anchor while loading.** Frames are counted through the load and
  the wall does not follow them, so an anchor taken before it leaves every later frame due
  far in the future.

`LF2_CLOCK_SITES` is what found the loop this had to accommodate; see the debug switches.

Frame rate is unchanged by the fix, measured rather than assumed: ~360 frames in 12 s
before and after, across three runs each.

**It does cost load time, and that is the right trade.** `LF2_NO_SLEEP=1` restores the old
no-op for A/B measurement: the data load finishes at **8.2 s** with sleeps skipped and
**13.1 s** with them honoured. The 5 s is the game's own pacing, not port overhead —
`LF2_SLEEP_DEBUG=1` shows it requesting ~4.7 ms sleeps about 170 times a second, 28 s of
intended sleep across 35 s of wall time, and `nanosleep` overshoot at that granularity is
tens of microseconds. Buying 5 s of load by burning a core continuously is not a trade
worth making, and `LF2_NO_SLEEP` is a measurement knob, not a tuning option.

For reference, Wine running the same binary sits at ~40% CPU on the menu where this port
sits at ~13%.

## Tests

```sh
cd scratch/build && ctest      # THE suite: 8 tests, ~1.3 s, nothing in it boots the game
tools/e2e.sh                   # the scripts that DO boot the game (seconds to minutes each)
tools/e2e.sh mouse render      # by name
```

There is one ctest suite and it is fast — that is deliberate, and it is a bar, not a
description. This is a 2001 game whose logic is arithmetic over a few megabytes; anything
here that took minutes was measuring the wrong thing in the wrong place. So what can be
checked without a running game is checked without one: `runtime/overrides/geom.h` holds the
port's pure geometry and `tests/test_geom.c` walks all of it in a millisecond, and the
overrides *include* that header rather than keeping their own copy of the arithmetic.

`tools/e2e.sh` keeps the rest — the questions only a running instance can answer. It runs
them one at a time and prints a summary that distinguishes a skip from a pass.

`tools/routes/smoke_test.sh` drives the port deep into the game headless and asserts what has
actually broken before: colour-keyed blits, sound effects firing, a non-zero mix peak, the
device being pulled, music decoding, and no aborts. Thresholds sit far below observed
values so it fails on "broken", not on "slightly different". It skips itself if the game
tree is absent.

It also guards CPU use, because a regression to busy-waiting is invisible to every other
assertion: the game renders, sounds and plays correctly at 96% of a core, which is exactly
how the unimplemented `Sleep` survived. `LF2_NO_SLEEP=1` is the control — it reports 99%
and fails, against 18% normally.

It does **not** guarantee reaching a running match. The scripted keys reach character
selection reliably, but whether the final presses land on "Fight!" or on "Reset Random"
depends on when the pre-fight overlay opens relative to the ~13 s load, which varies run to
run. Frame dumps caught runs ending at character select while still reporting healthy blit
and audio counts — so the earlier claim that the numbers came from a match was wrong. The
assertions are unaffected, since each was checked against a broken build and failed there
either way.

**It is validated against deliberately broken builds**, which is the only reason to
believe it. Reintroducing the ADC/SBB carry bug makes it report `keyed blits: 0` and fail;
removing it again passes at ~11,600. That check also caught a bug in the test itself:
`grep -oE 'keyed blits=[0-9]+'` matches inside *un*`keyed blits=`, so with `tail -1` it had
been asserting on the unkeyed count all along — a number that is large whether or not
colour-keying works. The assertion could not have failed. The pattern is now anchored on a
leading space.

## Deterministic frame capture

`LF2_FRAME_DUMP=1900,2100` writes those presented frames as PPM into `$LF2_DUMP_DIR`
(default `scratch`):

```sh
cd game && SDL_VIDEODRIVER=offscreen LF2_DUMP_DIR=../scratch/frames \
  LF2_FRAME_DUMP=1900,2400 ../scratch/build/lf2 lf2.exe
```

Prefer this to screenshotting an X server. Frame numbers are exact and reproducible, it
works headless, and it captures the game's own framebuffer rather than a window with
whatever the desktop put behind it. Two attempts at photographing a match via `import`
landed on the preceding menu instead; the frame dump is what established that those runs
genuinely never reached the match.

`LF2_MEM_DUMP=<frame>[,...]` is the same idea for state: it writes the whole `.data`
section to `data_<frame>.bin`, and `tools/re/diff_data.py` compares two of them. Together they
answer "which variable is behind this pixel" — dump both, change one thing, diff. See the
scripted-input section above for the worked example.

## State-triggered input

`LF2_AUTOKEY_AFTER=0x68` starts the key script when the game is first seen polling that
key, instead of at a wall-clock offset from launch. It arms the read-watch on the key array
automatically. The point at which a menu starts asking about the player keys is a fact
about the game's state; "32 seconds in" is a guess that drifts with however long the data
load took.

In practice the trigger fires at the **mode menu**, which is the first screen to poll the
player keys — not at character selection, as first assumed. That still removes the load-time
variance, which was the largest source of drift.

**It does not make reaching a match deterministic, and honesty requires saying so.** The
remaining variance is at the pre-fight overlay: depending on when it opens, the same two
"up" presses either move the highlight to `Fight!` or close the overlay and leave character
selection with both slots re-rolled. Verified by frame dump, not inferred. Driving that step
reliably needs a signal for the overlay itself, which does not exist yet.

## Quitting

The game exits through the CRT's `exit()`, not by returning from its entry point, so SDL
teardown is registered with `atexit` — a call placed after `dispatch()` in `main` would
never run. Before this there was no teardown at all.

`LF2_QUIT_AFTER=<frames>` posts `WM_QUIT` once that many frames have been presented, which
is how the shutdown path gets exercised in tests. Closing the window from a bare X server
does **not** exercise it: with no window manager the close becomes an `XDestroyWindow`, SDL
then touches a dead window, and Xlib kills the process before the game's own shutdown runs.
That produced a `BadWindow` error and exit status 1 which looked like a port bug and was an
artefact of the test.

The smoke test uses this so its run ends through the game's own shutdown rather than
`SIGTERM`, and asserts the exit status. Previously every run ended in `timeout`, so a
genuine crash on exit would have been indistinguishable from the kill.

## Import call volume

`LF2_IMPORT_STATS=1` reports the most-called imports at exit. The numbers are larger than
they look like they should be:

```
import calls: 7166995 total across 163 imports
  fscanf     2546142      timeGetTime   164614
  feof       2167756      PeekMessageA   80828
  fprintf    2161434      malloc          7108
```

Nearly seven million of those come from three CRT functions, because LF2 decrypts each
`.dat` into a temporary file and then parses it back token by token. That is the game's
design, not a port artefact.

It does mean the import path is hot. Resolving a handler used to walk up to seven lookup
tables doing two `strcmp`s per entry, **on every call**. Caching the resolution per import
took the data load from **13.1 s to 10.2 s** (measured twice, identical), and the cache is
correct by construction: the guest's import table is fixed once the image is loaded.

**Is 10 s slow? Yes, and the reason it looked acceptable was a bad question.** This
paragraph used to argue that Wine takes about as long, so nothing was left to win. Both
halves were wrong. The standard is not "no worse than Wine" — this is our port, and ten
seconds to a loading screen is unacceptable on its own terms. And the measurement was
weak: "screen content stops changing at ~11 s" by perceptual hash cannot tell the loading
screen from the mode menu, as the original text admitted while drawing a conclusion from
it anyway.

What the load actually is, measured with `LF2_SCAN_PROF=1` (which reports the span from
the first parse to the last, split into sleeping and working):

| component | time | share |
|---|---|---|
| `Sleep` (2036 calls) | 9.3 s | 65 % |
| other work | 5.1 s | 35 % |
| …of which `.dat` parsing | 0.34 s | 2.4 % |

**The load is bounded by wall-clock time, not by work.** `LF2_NO_SLEEP=1` only takes the
span from 14.4 s to 10.5 s, because the same loop spins more iterations against the same
deadline. That is why caching import resolution (13.1 → 10.2 s) disappointed: it was
optimising 5 % of the load. Ranking imports by *time* rather than call count says `Sleep`
is 24.7 s of the 27.1 s spent in handlers, while the 6.9 M `fscanf`/`feof`/`fprintf` calls
cost 0.79 s combined.

That paragraph's conclusion — "bounded by wall-clock time, not by work" — was **half right
and is superseded**; see the resolution below. The sleep really was 65% of it, and removing
it really did help. What the table could not show is that most of the *remaining* work was
not parsing or drawing either, but the game decrypting its data one byte at a time.

An earlier line here named `fn_004242e0`, the loading screen's own frame limiter, as the
gate. That was disproved by attempt 3 below: it is the ad grid and is not called during the
load at all.

**Correction to the blit-optimisation claim.** The commit that hoisted the per-pixel
divide out of `blit()` reported the non-sleeping component falling from 9.7-11.5 s to
6.0-9.4 s. That was measurement noise, not the change: those "before" numbers were taken
while this machine carried a load average above 20 from other work, and the "after" ones
while it had quietened. Measured properly, before and after are the same -- ~14.8 s span,
9.5 s asleep, 5.3 s working. The optimisation is real (417 equivalence cases, and it does
strictly less arithmetic per pixel) but it does **not** shorten the load, because the load
is sleep-bound rather than work-bound. Any load-time figure taken on this machine while
`uptime` shows a load average above ~5 should be discarded.

**Attempts to open the gate, all reverted, and why each failed.** 1. Pre-expiring the loading screen's own 33 ms deadline made loading *slower* — 34.8 s
   against 23.4 s — because each step calls `fn_0043f010` to redraw the screen. Removing
   the wait does not advance the load faster, it performs thousands more full redraws.
2. The same, with the repaint throttled to 33 ms: 19.2 s against 18.2 s, no gain.
3. Forcing the main loop's 3 ms tick path (`DAT_0044d02c = 0`) while the loading screen is
   up: the fast path **engaged for 9 frames of an entire run**. `fn_004242e0` is not called
   during the loading itself, so keying off it does not detect loading at all. The counter
   that reported this was added precisely because the previous attempt was judged by its
   effect rather than by whether it fired.

### Resolved: 8.4-10.5 s to 1.2 s, and it was never the drawing

Every attempt above aimed at the rendering because stack sampling pointed there. It was
wrong, and the way it was settled is the point: `LF2_LOAD_PROF=1` times `surf_Blt`,
`StretchBlt`, `present` and the colour fill **while the game is opening its data files**,
and prints their total against the active loading time. Drawing measured **0.48 s of
3.35 s — 14%**. A profile with a denominator ended an argument three guesses could not.

**The cost was the decryption.** `FUN_004148a0` turns one encrypted `.dat` into
`data\temporary.txt`, and it does it one byte at a time through the C runtime:

```c
fscanf(in, "%c", &c);  ...  fprintf(out, "%c", c - key[i]);
```

which is nothing natively and is 2.5 million guest→host import calls through a recompiled
CPU. It is now a native override:

| | |
|---|---|
| key | `SiuHungIsAGoodBearBecauseHeIsVeryGood`, 37 bytes |
| header | the first `0x7b` = 123 bytes are discarded, and the key index advances with them, so the payload starts at key index `123 % 37` = **12** |
| byte | `out = (in - key[i]) mod 256`, then `i = (i + 1) % 37` |

Index 12 is where `odBearBecauseHeIsVeryGood` begins, which is why that 25-character key —
the one that circulates — decrypts the first 25 bytes of a file and then turns to noise. It
is this key seen from its offset, with the wrap missing.

Byte-exactness is proved, not eyeballed: `LF2_DECRYPT_DUMP=<dir>` copies each decrypted file
out, and it lives in the **override** so the control run dumps too. One run with
`LF2_SLOW_DECRYPT=1` (the game's own loop) against one without gives 77 files, 2.2 MB,
**all byte-identical**.

**And a skipped sleep now credits the guest clock.** Skipping the frame-pacing `Sleep`
without moving the clock does not end the caller's deadline loop — it converts the wait into
a spin. That was 142,721 skipped sleeps in a 3.35 s load, 453 per data file. Crediting the
requested time drops it to ~1,900. It costs about 0.10 s of loading (1.08 s against 1.19 s,
measured both ways) and buys back 145,000 pointless import dispatches; the whole run
finishes 2.7 s sooner.

**Dead end, measured, do not retry: scaling the guest clock.** Running time faster during
the load gives 3.6 s at 1x, 3.5 s at 4x, 4.7 s at 16x, 7.0 s at 32x. A jumping clock makes
the game do *more* catch-up work. The lever is fast-forwarding through a wait the port
decided to skip, not running the clock fast.

**How the loader was found.** `LF2_LOAD_SITES=1` lists the distinct guest return addresses
that open data files, with a count and the first path for each. Thirteen sites, and the
shape gives the structure away: every object file is opened, decrypted to
`data/temporary.txt`, and reopened, so the sites come in pairs with matching counts —
77/77, 65/65, 12/12.

**What is left.** 1.2 s, of which 74% now genuinely *is* drawing: 2782 `surf_Blt`, 363
`StretchBlt`, 394 colour fills and 342 presents across the load. Cutting it means fewer
loading-screen repaints — and note that presented frames are what `tools/*_test.sh` schedule
their input against, so anything that changes the frame count shifts every scripted route.

An earlier follow-up did **not** pay off, recorded so it is not tried again: `h_fscanf`
called `getenv` on every one of those 2.5M invocations, and caching it changed the load time
by nothing measurable (10.1 s / 10.3 s against 10.2 s). glibc's `getenv` is cheap next to the
surrounding parse. The caching was kept because it is the right shape for a flag on a hot
path, not because it bought anything.

## One keyboard, first come first served

There is exactly one keyboard layout, and it is drawn along the bottom of the front end:

    arrows move | Z attack | X jump | C defend

The four per-player layouts from `data/control.txt` no longer reach the game; the control
settings screen still edits them, but the input gather override replaces every live
player's buttons with the port's own device routing (`runtime/overrides/input.c`).

Devices — the keyboard and every connected pad — are handed to players **first come,
first served**: outside the game proper every device drives player one, so anyone can
work the front-end menus; from the mode menu onward the first device to press anything
becomes player 1, the next player 2, and so on, and pressing attack on the join screen
claims and joins in one stroke. Assignments clear when the game returns to the front
end, so the next session reassigns from scratch. Held keyboard state comes from a
host-side ledger fed by the same message stream as everything else
(`hostwin_key_held`), which is what makes the scripted-key tests exercise the identical
path a human uses.

## Playing with a controller

Plug a pad in and play. Nothing to configure, and the keyboard keeps working at the same
time.

| control | effect |
|---|---|
| d-pad / left stick | the player's directions; in the front-end menu, moves the selection |
| A (south) | attack; in the front-end menu, activates the selection |
| B / X | jump / defend |
| Start | activates the front-end menu's selection |

Pads are handed to live player slots in order, so **a second controller is player two**,
with no configuration either. `LF2_VIRTUAL_PAD2` attaches a second software pad and
`tools/routes/controller_2p_test.sh` (ctest target `controller_2p`) asserts it.

This claim has been wrong in both directions, which is worth recording:

- It was first written down when only **one** virtual pad had ever been attached, so the
  slot-assignment code had never run with two. True by luck, not by test.
- It was then **retracted** on the finding that a second pad drives a *computer's* fighter.
  That was a mistake in the test, not a defect in the port: on the character-select screen
  an unjoined slot shows `Join?`, and it is filled with a computer only once **player one
  proceeds** past the screen. The second pad had been pressing after that point, so the
  slot was already taken.

That it is not a countdown was measured rather than assumed — with one pad and no further
input, slot 2 was still `Join?` at frame 2400 and the word `Computer` was never drawn.

Verified: two pads attach and bind; the second joins as **Player 2** and picks its own
fighter (its d-pad selected Davis while player one stayed on Random); the merge counters
read 1408 across 704 gathers, exactly two per gather.

The test is two-sided on purpose. "The word `Computer` was never drawn" also holds for a run
that never reached the screen, so a control run without the second pad asserts that
`Computer` **is** drawn. Two drafts of the test stopped too early and the control caught
both.

### Why this needed a port rather than a shim

`joyGetPosEx` and friends are reimplemented on SDL3 in `runtime/input/gamepad.c`, and they were
answering correctly long before a controller did anything useful. The reason nothing
happened is one level up: **a controller reaches a player only if that player's control
config names a joystick**, and nothing sets that without a trip to the settings screen.

That decision lives in `fn_00419a60`, the per-frame input gather, so that is what is
ported (`runtime/overrides/`). Read out of the original:

| address | meaning |
|---|---|
| `0x00450b4c[i]` | device selector for player `i`, `i` in 0..7; `<= 0` means the slot takes no live input (unjoined, or −1 while a recording plays). Loop bound `0x00450b6c`. |
| `0x0044fbe0` | control configs, stride 80. `[+0]` is a joystick number, 0 for keyboard; `[+4..+28]` the seven keyboard codes, `[+32..+40]` joystick buttons. |
| `this+404` | array of eight player-object pointers |
| `obj+198..204` | last frame's buttons; `obj+205..211` this frame's, one byte each, in the order up, down, left, right, attack, jump, defend |
| `0x00450b80` | non-zero while the packed per-player masks are being consumed |
| `0x0044f1af` | recording flag; gates the `0x0044d040` mirror of those masks |

The port runs the original first, so every configured device behaves exactly as before,
then **merges** any connected pad into the buttons of the player it belongs to — merged,
not substituted, which is what keeps the keyboard alive for the same player. The packed
masks get the pad's presses too, or a recording made with a controller would replay as a
player standing still.

Everything the game drives from those buttons then follows for free: mode select, character
selection, the pre-fight overlay and the match itself.

The front-end menu is the one exception, because it is mouse-driven rather than
button-driven, so what a pad moves there is the ported menu's selection index. Selecting
works by placing the pointer, so the game highlights the entry exactly as a mouse would —
the highlight is the game's, not something drawn on top. The band coordinates come from the
game's own hit-test constants (`tools/re/click_bands.py`).

This replaced an earlier version that synthesised player-1 keypresses at the window
boundary for all of it. That worked, but it made a controller pretend to be a keyboard
instead of being an input device the game understands, it could only ever drive player one,
and it silently assumed player one still had the default key bindings.

### Testing it without a controller

`LF2_VIRTUAL_PAD="down:220,down:250,south:290"` attaches a **software** gamepad through
SDL and plays a script of button presses into it — names are SDL button short names, the
number is the frame to press on, released eight frames later.

This is how the controller support is tested at all, and it verified things that had been
untestable and were therefore pure assumption:

- **auto-detect** — the runtime reports `controller 0 connected: lf2 virtual pad`
- **hotswap** — the pad attaches *after* the game has started and already probed its
  joysticks, and is still picked up, which is exactly the case the stock game cannot handle
- **the whole route** — `tools/routes/controller_test.sh` (ctest target `controller`) drives from
  the title screen into character selection with **no keyboard and no mouse input at all**,
  and asserts on the input gather's own counters

That last one is a regression test with a specific bug behind it: a wrong gate on the
ported menu disabled the port outright, the game quietly fell back to its original body,
and every other test stayed green. The test was checked by re-introducing that bug on
purpose — it fails, three assertions at a time.

`input: N gathers, N live player-slots, N of them with a pad, N button presses merged` is
printed every 900 frames. All four are counters rather than a hit log, because the failure
worth catching is that nothing is ever merged, and a line that only printed on success
would be silent in exactly that case. The three totals separate "no player slot was live"
from "no controller was bound to one" from "a pad was there and nothing was pressed".

Real hardware is still worth testing, since SDL's virtual device cannot reproduce every
driver quirk. But the code path is no longer unexercised.

## The ported menu

`fn_004246b0` (the menu) is being ported incrementally in `runtime/overrides/menu.c`. What is
native so far is the **selection**: a real index the port owns, rather than something
derived from where a pointer happens to be. Everything else still delegates to the original
body through `fn_004246b0__orig`.

The menu's own state, from its disassembly:

| address | meaning |
|---|---|
| `0x004546f0` / `0x00453cdc` | mouse x / y, which the hit test brackets at x 260..547 |
| `0x00457580` | click flag, compared against 1 before an item activates |
| `0x0044d064` | the action the chosen item sets |

The port moves the selection, places the pointer on the chosen item so the **game's own**
renderer highlights it, and raises the game's click flag to activate — so the highlight,
the click sound, and the screen change all remain the game's code. Nothing is drawn or
dispatched by the port.

A controller drives it directly (`runtime/input/gamepad.c`), and a mouse still works as before.
The two are kept consistent: if the pointer moves to somewhere the port did not put it, a
mouse is being used, so whatever it is pointing at becomes the selection and the port hands
control back. Picking up the mouse after using a pad does not fight it, and vice versa.

`fn_004246b0` is a `__thiscall` method and **`[this+0]` is the top-level mode**. Its own
dispatch:

| `[this+0]` | |
|---|---|
| 1 | a one-shot entry step that immediately stores 2 and returns |
| 2 | hand the frame to `fn_0041bc90` — character selection and the match |
| anything else | fall through into the front-end menu body |

So **the front end is the default branch, not a numbered mode**; the value observed there is
0. An earlier version of this documentation said mode 1 was the front end, and the gate was
written to match. Mode 1 is the one value that is never live for a whole frame, so the port
never ran at all: the game used its original body, the menus worked, the ads were still
gone (that is a separate override), and every test stayed green. A dead port and a working
port were indistinguishable from the outside — which is why `tools/routes/controller_test.sh`
exists and why it was checked against the bug re-introduced on purpose.

`0x0044d064` is only the sub-screen *within* the front end, so both are checked — keying
off the sub-screen alone would let these tables fire during character selection whenever
that variable happened to hold a matching value.

**Character selection needs no menu port.** `fn_0041bc90` is 7499 lines and reads the mouse
zero times: it is driven entirely by the player buttons, which is the input idiom the game
itself uses there. What a controller needs to reach it is the ported input gather below,
not a table of clickable bands.

Screens are ported one at a time, each with a table of selectable items taken from the
game's **own hit-test constants** — the centre of each band it brackets the pointer
against. Anything without a table is pure delegation.

| screen (`0x0044d064`) | items |
|---|---|
| 0 — main menu | five entries, x 260..547 |
| 6 — control settings | ok (x 405..560, y 441..465), cancel (x 582..737, y 441..465) |
| 7 — recording info | ok (x 231..386, y 416..440), cancel (x 403..558, y 416..440) |

Adding a screen means reading its comparisons out of the disassembly, not inventing
coordinates — worth insisting on, since a screenshot estimate of the control-settings
button row was out by 26 pixels. `0xFFFFFFFE` appears during loading.

The recording page's "click here to know more" link is deliberately **not** selectable: it
opens a web page, and a controller should not be able to land on something that leaves the
game.

Verified with `LF2_VIRTUAL_PAD`: three d-pad downs highlight "recording info", two downs
plus A lands on the control settings page, and the mouse-driven smoke test is unaffected.

## Every menu takes every device

The chain from the launcher to a running match is mouse-drivable end to end, with no key
and no pad: `tools/routes/mouse_test.sh` is that route, and `ctest -R mouse` runs it.

| screen | selection | how it was located |
|---|---|---|
| launcher (0/6/7) | the game's own index | its hit-test constants, read out of the disassembly |
| mode menu | `0x00451160`, 0..7 | `.data` diff across one down-press against a no-press control |
| character select | `+0x364` in the object at `0x00458c94[1+player]`, 0..7 | heap diff — it is not in `.data` |
| pre-fight overlay | `0x0044d06c`, 0..5 | `.data` diff across one d-pad press |

The overlay also needed a **screen discriminator**, which character selection had gone
without: it sits *on top of* character selection, so both hit tests are live at once and the
pointer would drag the slot cursor while the player aims at "Fight!". `0x0044d070` is that
word — `-100` while players join and pick, `0` while the overlay is up, `1` once the match
runs. Found by diffing two overlay-closed frames against two overlay-open ones, keeping only
dwords stable within each pair (26 candidates), then checking each against a third state.

Row geometry comes from the game, not from measuring a screenshot: `LF2_OVERLAY_FORCE=<n>`
pins the selection and `LF2_BLT_FRAME` prints where the game blits its own highlight — item
0 at y 16, item 2 at y 64, item 5 at y 137, i.e. 24 per row from 16, in the panel's x band
of 3..307.

### The scripted click tested hover and nothing else

`LF2_CLICK_SCRIPT` pushed the `WM_LBUTTONDOWN` the *game* reads but never armed
`hostwin_mouse_clicked()`, which is what the *ported* menus read. So no scripted run had
ever exercised a click activating anything — the key script that followed confirmed whatever
the hover had selected, and a menu whose click was dead looked identical to one that worked.
That is why the mouse now has its own test with no keyboard in it at all. Run against both
classes: with the click edge disabled the route stops at the mode menu (2 screen
transitions, 1 sound effect); with it live it reaches a match (4 and 10).

## Finding which code draws something

Three hooks in `runtime/video/ddraw.c`:

- **`LF2_BLT_FRAME=<frame>[,...]`** logs *every* blit that composes those presented frames —
  both rectangles, the source surface and its size, the flags, the calling guest address —
  and finishes with `bltframe <n>: N blits total`. A match frame is about 140 blits, so the
  list is complete and still readable, and the frame numbers are the ones `LF2_FRAME_DUMP`
  uses, so a dump and its blit list line up exactly.

  Colour fills print `COLORFILL=<argb>`, and the hook runs **before** the colour-fill branch
  on purpose: that branch returns early, so a hook after it cannot see a fill at all. That
  blind spot produced a confident wrong answer once — "the stage ground is drawn by four
  layer blits and nothing else", with the fill underneath them invisible (issue #9).
- **`LF2_BLT_RECTS=1`** logs every blit destination rectangle. That is how the ad regions
  were enumerated: top strip `(0,0)-(397,34)`, right panel `(590,199)-(788,393)`, its arrows,
  and the bottom row — against the character art at `(0,0)-(330,546)`.
- **`LF2_BLT_STACK=<x>,<y>`** walks the guest stack at the first blit landing on that
  destination and prints everything that looks like a `.text` address. It is a rough
  backtrace, not an exact one, but it is enough to **diff two chains**.

- **`LF2_PRIMARY_STALE=1`** injects the defect that issue #29 was about: the copy from the
  composition to the primary skips `(composition - 794) / 2` columns on the left, so they keep
  the previous size's pixels. It is the negative arm of `tools/e2e.sh resize` -- "the band left
  of the panel is black" would pass just as happily on a frame that is black everywhere, so the
  check has to be shown failing. It used to *disable a clear*; issue #42 made the copy 1:1, so
  every column of the primary is written every frame, the ghost has nowhere to live and there
  is no clear left to disable. The number of columns matters and is not a round figure: the
  leftmost columns are black in every frame at any one size, so skipping an arbitrary 64 left
  the injected and clean runs agreeing and the arm unable to fail. The ghost only exists where
  a *differently* centred screen had picture.

- **`LF2_DRAW_PATHS=1`** counts every route that can carry pixels — `Blt`, `BltFast`, and the
  game writing into a surface between `Lock` and `Unlock` — and reports them together every
  900 frames. The Lock route is counted by whether the pixels actually **changed**, since a
  lock taken to read is not a draw. Measured over a full run to a match:
  `Blt=19753 BltFast=0 Lock=0`, i.e. the game draws through one COM method (issue #30).

  It prints all three counters including the zeros, and prints what it *cannot* see every time
  rather than only when it finds nothing: GDI text goes straight into the surface without a
  Lock (`runtime/win32/gdi.c`), and a lock whose writes cancelled out would hash the same.

### The native renderer, the lighting and the cast shadows

- **`LF2_RENDERER=soft`** presents the software compositor instead of the GPU renderer. Both
  build every frame; this chooses which one is shown, and it is how `tools/e2e.sh render` diffs them.
- **`LF2_HD2D=off`** turns off the lighting *and* the sprite-cast shadows, which restores the
  game's own dithered ellipse. The lighting is **on by default** — it is a look, not a switch;
  this exists so the renderer's *geometry* can be compared against the software path with
  nothing on top of it, and so the pass can be shown to do something.

  **It is a control for "is the pass running", NOT for any single term inside it** (instrument
  I011). Diffing a lit frame against an `LF2_HD2D=off` frame and attributing the difference to
  one term is wrong in a way that looks right — most of a fighter's body changes under that
  diff, so the marked-up frame reads as "the shadow is all over the fighter" whatever the
  shadow is doing. On a match frame at 1920×1080 the whole pass moves 1837–2245 pixels and the
  cast shadow alone moves 271–713. **The per-term control is the knob**: `LF2_HD2D_SHADOW=0`
  against the default isolates the cast shadow with the rest of the pass still running, and
  `LF2_HD2D_SHOW=chars` gives the character mask to test a pixel against rather than guessing
  from a screenshot. That pair is what settled issue #48.
- **`LF2_HD2D_SHOW=albedo|chars|shadow`** presents one buffer of the chain instead of the lit
  frame: the composition as the game drew it, the character mask with its height channel, or
  the cast-shadow mask. Every fault in the lighting looks the same from outside — a
  picture that is slightly wrong — and this is what tells "the bevel is too tight" apart from
  "the character mask is empty". Each buffer is shown at the moment it is *final*; the first
  version showed them all at the end of the chain, by which time the shadow scratch had been
  reused, and `SHOW=shadow` confidently displayed a blurred copy of the scene.
- **`LF2_HD2D_LIGHT=<azimuth>,<elevation>`** puts the key light at a known angle, in degrees.
  The light is the *player's* — it is set from the pause menu's **Options** page — and this
  exists so a test can place it and check the shadows actually followed. "The shadow's shape
  responds to the light" is not something one screenshot can show; two are needed, and this is
  how they are taken.
- **`LF2_HD2D_KEY`, `_AMBIENT`, `_BEVEL`, `_BEVEL_PX`, `_HEIGHT_GAIN`, `_SHADOW`,
  `_FLOOR_FEATHER`** sweep the light rig while it is being tuned. They are not configuration.
  The defaults are chosen so a flat, unshadowed, camera-facing pixel comes out at almost
  exactly the colour the game drew it: the light must be a *difference from flat*, not a
  brightness or a tint laid over the game.
- **`LF2_OVERLAY_DEBUG=1`** prints, every frame the pre-fight overlay is up, the pointer, the
  row it resolves to and the current selection. It is what found both halves of issue #26: that
  an idle pointer left inside the panel band was dragging the selection to *Exit* on the frame
  the overlay opened, and that the row geometry the hit test used was not the game's.
- **`LF2_SHADOW_DEBUG=1`** reports which object the loaded stage draws its shadow ellipse on,
  and how often that identification fires. The object is learned per stage from `bg.dat`'s
  `shadowsize:`, not read from a fixed offset — the offset that looked right matched 0 of
  40000 draws (claim C019).
- **`LF2_RENDER_SKIP=<n>`** drops every nth display-list entry. It is the negative arm of
  `tools/e2e.sh render`: this comparison was fooled once already (the readback ran before the draw, so
  every dump was the previous frame — which with a scrolling camera looked like a clean
  one-pixel shift), so an arm that draws the frame *wrong* has to come out different.
- **`LF2_RENDER_DEBUG=1`** reports what each frame was made of — quads, fills, tiles, cached
  textures, uploads, dropped entries, ground markers, cast shadows and how many frames the
  light ran on. It prints the zeros too, and says explicitly when the GPU path presented no
  frames at all, when entries were dropped, or when the shaders never loaded and why, because
  "0 quads" and "the renderer was never called" are different faults. Two lines in it are
  worth knowing by name:
  - `gpu=on frames=N` — **N is the number the renderer actually drew**, and `frames=0` beside
    `software fallbacks=0` means it was never asked rather than that nothing went wrong. Both
    counters live inside `render_present`, so a frame the present gate skips increments
    neither. This is how the menus were stretched by SDL for the whole life of the renderer
    without a single test noticing (issue #52).
  - `N frame(s) were drawn over a RETAINED list` — frames the game recorded nothing for and
    the renderer redrew what it already had, which is how the pause menu is presented. Zero is
    the correct answer for a run that never paused, and `tools/e2e.sh pause_dropout` asserts
    both directions: non-zero in the run that pauses, zero in the same route without one.
  - `the busiest frame drew N tile(s) against a pool of M` — the tile-texture pool must be at
    least as large as the busiest frame, because every tile in a frame is live at once. When
    N reaches M the report says how many tiles were dropped, and a dropped tile is text
    missing from the picture.

The renderer draws the display list into a target the size of the window, scaling **each quad**
by the world scale as it goes (`SDL_SCALEMODE_NEAREST` throughout, so a sprite's texels are
resampled once, from the source art). Frame dumps in GPU mode are therefore the size of the
window, not of the composition.

**What the lighting touches, and what it does not.** One key light, given as a direction in the
stage's own three axes (x across, y up — LF2's jump axis, claim C018 — z toward the camera).
It **shades** only the objects standing in the stage, which the game itself identifies by
drawing a shadow ellipse at their feet immediately before drawing them. The HUD, the text and
any leftover band are never touched at all. Two things do reach the stage, and both are
geometry rather than decoration: the **cast shadow**, which is the point of a cast shadow, and
the **floor's orientation** below — a hue shift at locked luminance, so it cannot change how
bright the picture is.

On a frame with no stage and no fighters in it — the menu, character selection — the pass
changes **nothing at all**, byte for byte, and `tools/e2e.sh render` asserts that alongside asserting
that the match frame *does* change. An effect that has quietly spread over the whole picture
passes the second check and fails the first, which is exactly how the bloom-and-haze version
of this pass would have been caught.

The shadow is the sprite's own silhouette **projected** onto the ground: a point at height *h*
lands at `h * (-Lx/Ly, Lz/Ly)` in screen units, and both terms come from the same light vector
the shading uses. So a low light throws a long shadow, an overhead one throws almost none, and
a fighter in the air has their shadow displaced along the light rather than welded under them.
Measured at two elevations on the same frame: 13102 shadowed floor pixels spanning 311 rows at
20°, against 5131 spanning 203 rows at 85°. An earlier version used the sideways term and a
**constant** 0.30 in place of the other, which is why moving the light changed where a shadow
pointed and never how long it was. It is **crisp**: an earlier version
downsampled the mask to half resolution and blurred it, which is what a soft shadow wants and
is not what this game wants — a 32-pixel sprite's silhouette, halved and blurred, is a
shapeless dark smear with none of the fighter left in it. Its mask needs its own shader for a
reason worth knowing: SDL multiplies the vertex colour into the texture, so the fixed-function
route gives `sprite.rgb * a` — the fighter's colours — and the shadow came out darker under
the bright parts of them.

**The floor is lit as a floor.** `bg.dat`'s `zboundary:` is where the walkable floor begins on
the screen, because LF2's depth axis projects straight down it — that is why the game can
depth-sort on z and why it puts the shadow ellipse at `y = z`. So the rows below it are a
horizontal surface and the rows above are a wall, and the two see different amounts of sky.
That difference is applied **as colour**: the tint is the ratio of the two lighting terms with
each normalised to unit luminance, so the ground picks up the sky's colour without the picture
getting brighter or darker. Measured over a match frame's floor with the fighters excluded,
the mean luminance change is **-0.03 levels of 255** against a mean chroma change of **3.0**.
It is a per-channel multiplier, so a strongly saturated pixel can still move in luminance —
what is locked is the tint, not every pixel. The backdrop's multiplier is exactly 1: measured,
**0 of 16880** sampled backdrop pixels changed, which is what lets the pass keep its promise
that a frame with no stage and no fighters comes out byte-identical. It is gated on the in-match HUD: the
background record stays loaded after a fight, so without that gate the front end would have
its lower half tinted by a stage that is not on screen.

**The shaders are compiled SPIR-V, committed** in `runtime/shaders/gen/`, so the build still
needs nothing but a C compiler and SDL. `tools/build/build_shaders.sh` regenerates them and
`ctest shaders` recompiles and compares wherever `glslc` is present, so an edited shader that
was never recompiled fails the build rather than shipping last week's lighting. On a GPU
backend that does not take SPIR-V (Metal, D3D12) the port says so on stderr and presents the
plain composition; there is deliberately no approximation to fall back to.

### The stage's own background layers

- **`LF2_BG_ORIG=1`** hands the background layer draw back to the recompiled body instead of
  `runtime/overrides/background.c`, and **`LF2_BG_SKEW=<n>`** shifts every layer's parallax
  offset by `n` pixels. Both exist for one purpose: `tools/e2e.sh background` runs the same route
  three ways and asserts the port's frames are byte-identical to the original's *and* that
  the skewed arm differs. Without the third arm, "the two agreed" would be indistinguishable
  from "the dump does not contain the background at all".


- **`LF2_GLYPH_POS=1`** prints every glyph drawn from one of the game's three font sheets with
  the position **the game asked for**, before any of the port's offsets. It exists because the
  fighters' name tags go out this way — not through `TextOutA`, not through the game's string
  helper `fn_00423940`, both eliminated — so this is the only place their x can be *read*
  rather than inferred from a screenshot. Each line also carries the **return address** of the
  call, which is what named the tag's draw (`0041ab26`, inside `FUN_0041a5a0`), and the
  **camera** as both the game's word (`cam=`) and what `bg_draw_camera` returns (`draw=`).
  The tags are the run at y 398 in a match; the pair at y 531/532 is the stage-mode caption
  (issue #60).

  **Read `cam=` before concluding anything about widescreen from an x.** `bg_draw_camera`
  clamps at zero, so on a frame where the camera has not left the stage's left edge `cam` and
  `draw` are equal and a shifted draw is indistinguishable from an unshifted one. An x that is
  identical at 794 and at 1920×1080 was twice written up as proof the tag misses the camera
  shift; both runs had `cam=0` throughout, because the stage-mode route stands still after the
  overlay confirm. Hold `right` long enough to move the camera, or the run is not evidence
  (issue #55, instrument I012).

- **`LF2_OBJ_SKEW=<n>`** moves the camera the stage's OBJECT pass draws from by *n*, so every
  fighter, shadow, name tag and effect it draws moves *n* pixels and nothing else in the frame
  does. It is not a fix and not a port: it is the **negative control** for the byte-identity
  gate issue #55's hand-port of `fn_0041a5a0` will need, built before the port so that the gate
  has been seen to fail before it is trusted. `tools/e2e.sh objects` uses it — two default runs
  must be byte-identical, and a skew of 3 must change thousands of pixels.

- **`LF2_ALTBG_FORCE=1`** draws the game's **built-in background** (index 99) instead of the
  loaded stage's layers, and **`LF2_ALTBG_ORIG=1`** runs the recompiled body of it rather than
  the port. Together they are the A/B for issue #58. The force writes the game's own background
  index word — the same word `0042d7f6` writes 99 into when the stage chooser lands there — so
  it takes the branch the game takes rather than faking a screen. Without it that backdrop is
  unreachable from any scripted route, because every route takes the default background.

- **`LF2_BAND_DEBUG=1`** prints every blit that is a candidate for the full-width backdrop
  stretch — anything drawn from x 0 into the composition during a match — with the rectangle it
  actually arrived as. It exists because that rule tests `dr == NATIVE_W` on the assumption the
  *game* clipped the layer to its own 794, and the port now patches the game's width words to
  the view, so what arrives is worth printing rather than reasoning about. It is what showed
  that two blits with identical rectangles (`forests.bmp` and `forestm1.bmp`, both `dr 800`)
  need opposite treatment, and therefore that no test written at that call site can be right
  (issue #23).

- **`LF2_TEXT_DEBUG=1`** prints every string the game draws through `TextOutA`, with its
  position **and its destination surface and size**. The destination is the point: the two
  widescreen offsets in `h_TextOutA` are each decided by it — `hud_offset_x` by the band the y
  falls in, `screen_offset_x` by whether the surface is wider than 794 — and a line carrying
  only the final x cannot show which of them declined or why. That is how issue #54 was
  settled, and it overturned two earlier guesses about which branch was at fault.

- **`LF2_BG_TABLE=1`** prints the loaded stage's layer table once, the first frame a match is
  actually on screen. **`LF2_BG_TABLE=all`** prints *every* background record the registry
  holds — all twelve — regardless of which one is loaded.

  Each layer line is `span` (bg.dat's `width:`, how far the layer scrolls and how far its
  content reaches), `x`, `y`, and `loop` (bg.dat's `loop:`, the horizontal repeat step, `0`
  for a layer that is drawn once). Those four are what `fn_0041a250` draws from, and claim
  C017 has the formula.

  Use `all`, not `1`, whenever the point is to *check* the table: VS mode picked the same
  stage on six consecutive headless runs, so "run it again for a different background" is not
  a way to get a second sample, while one `all` run yields twelve.

  ```sh
  cd game && SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
      LF2_VIRTUAL_PAD="$ROUTE" LF2_BG_TABLE=all LF2_QUIT_AFTER=2300 \
      ../scratch/build/lf2 lf2.exe 2>&1 | grep "bg table" > ../scratch/alltables.txt
  tools/re/bg_table_check.py scratch/alltables.txt          # 12/12 against the shipped bg.dat
  tools/re/bg_table_check.py --selftest                     # asserts it can also say FAIL
  ```

  `tools/re/bg_table_check.py` is the reason the table is trusted (instrument I006): it decrypts
  every `bg.dat` offline and matches each runtime record to a file by full geometry, with no
  assumed ordering. It exits non-zero, saying it compared **nothing**, if the log holds no
  table lines or the game tree holds no `bg.dat` — a missing corpus must not read as a pass.

## Finding which code draws something — the blit chains

The diff is the point. The chain for the ad panel and the chain for the character art share
their entire outer frame and differ in one hop: the art goes out through `fn_00423840`, the
ad through `fn_00423b00` with descriptor `0x0044d060`. Neither could be identified by
reading the code — the ad-file strings have no cross-references in the disassembly at all,
and the one function that *does* reference the advertise URL turned out to be a shared
helper whose stubbing garbled the artwork.

Use these before stubbing anything. Two functions were stubbed on a plausible reading and
both were wrong; the differential trace got it right first time.

`LF2_BLT_STACK` matches on the **exact** top-left corner, so a coordinate one pixel out
finds nothing. It used to print nothing in that case, which reads identically to "that
rectangle is never drawn" — it now reports the miss and the nearest destination it did see,
and says so separately if the run produced no blits at all.

### The update notice — what was left after the ads

`LF2_BLT_RECTS` showed one destination still alive in the top-right corner of the menu,
`(725,5)-(787,18)`: a small **"Update on <date>"** label, drawn every frame and clickable.
It belongs to the ad system — the part that reads `data/adinfo.txt` and `data/ad0.txt`, the
latter being a list of banner rectangles and click-through URLs. Its click opens sub-screen
−3, an update page that can never do anything here because WININET is stubbed.

The menu's own code around it:

```
EnterCriticalSection(&ad_lock);  state = [0x00458424];  LeaveCriticalSection(...)
if (state == 1 || state == 2)  draw clip 0x0b at (725,5)      // busy, no link
else                           draw clip 6/7 at (725,5)       // idle, clickable
                               if (mouse.x >= 725 && mouse.y < 18 && clicked)
                                   sub_screen = -3
```

The state is 0 at runtime (measured, `LF2_MENU_DEBUG` prints it), so the live case is the
clickable one. Both halves had to go: `fn_0043f010` declines that clip of `MENU_CLIP7` at
that position, and the menu port swallows a click in the corner, because a removed control
that still responds is worse than one that is merely invisible.

Declining a draw by its identity is the same shape as declining the ad panel by its
descriptor. The alternative — porting `fn_004246b0`'s body around the block — is 4689 lines
of generated C with no function boundary anywhere near it, which is worth doing eventually
but not in order to remove one label.

### The root fix: the ad loader, not the presenters

The per-draw declines above turned out to be chasing presenters, and there are more of
them than the main menu shows: the **loading screen** draws a full-screen ad grid plus a
"To advertise on LF2" link through `fn_004242e0`, and the **mode menu** draws the same
panel as the front menu but under a different element descriptor (`0x0044d020`, not
`0x0044d060`). None of this was visible on the development machine, whose
`data/adinfo.txt` had long ago been reset to the factory default `now 0 4` — the game's
own ad system had written it after failing to reach its (dead) ad server, so every
"ads are gone" check here was passing against empty ad tables. A fresh install from the
installer has a populated ad set and showed all of it (docs/issues/0001).

The fix is one override at the root: `fn_0043c4a0`, the ad-set load (reads
`data/adinfo.txt`, parses `data/ad<n>.txt`, loads `sprite/sys/ad<n>.bmp`), returns 0.
Its one caller — the ad-system init `fn_0043cf40` — then falls to its own fallback
`fn_0043c690`, which resets `adinfo.txt` to the factory default and loads nothing, and
every presenter draws nothing because every one of them already handles empty tables.
The loading screen's advertise link is the one draw not gated on those tables; it is
declined at its single call site in `fn_0043f010`. Verified by frame dump on the loading
screen and the mode menu against a fresh install: both clean, and the smoke and
controller tests pass.

Two near misses recorded so they are not retried: `fn_0043c240` also parses `ad<n>.txt`
but never runs at boot, and `fn_0043bec0` — gating the same init — is the DirectDraw
init check, whose failure path is `DirectDraw Init FAILED`.
