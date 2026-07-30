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
Linux-only headers. Two real blockers were fixed:

- `MAP_NORESERVE` does not exist on macOS. It is only ever a hint on Linux, so it is now
  defined to zero where absent.
- `MAP_32BIT` is a Linux extension, used only by the instruction differential test.

**Apple Silicon runs the port but not one of its tests.** The recompiled game is ordinary
C and compiles for arm64 like anything else. The instruction differential test is
different: it works by executing the binary's *own* x86 bytes on the host and comparing,
which has no meaning on an ARM CPU. It now detects a non-x86 host and skips with a
message rather than failing. The decoder and flag tests are pure C and still run.

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

## Window mode

`LF2_WINDOW` selects how the window is created. The game itself only ever asks for a
fixed-size bordered window, so this is the port's choice rather than something the game
can express.

| Value | Effect |
|---|---|
| `windowed` (default) | resizable bordered window at the game's native 794x550 |
| `borderless` | same size, no frame |
| `fullscreen` | borderless fullscreen |

**Alt+Enter** toggles fullscreen at any time. Rendering always happens at 794x550 and is
letterboxed to whatever the window becomes, so the game never sees a different resolution.

## Scripted input

For checking the port without a human at the keyboard:

| Variable | Effect |
|---|---|
| `LF2_AUTOKEY=<vk>[,<vk>...]` | press each virtual key in turn, held 120 ms |
| `LF2_AUTOKEY_START=<ms>` | when to begin (default 6000) |
| `LF2_AUTOKEY_EVERY=<ms>` | gap between presses (default 2500) |
| `LF2_AUTOCLICK=<x>,<y>` | move the pointer there in game coordinates and click on the same schedule |

LF2's menu is driven by the player-1 controls from `data/control.txt`, not by Enter — the
defaults are the numpad, so `LF2_AUTOKEY=65` is player 1's attack.

**This does not yet advance past the menu.** Two hypotheses have been tried and both
failed: the port now delivers `WM_KEYDOWN`/`WM_KEYUP` from real key events *and* scripted
keys generate the same messages, so neither the polling path nor the message path is the
missing piece.

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

**The game still does not select a menu item.** The mouse is probably not how it is done:
every hit-test against `004546f0` compares x against 0x24e–0x2d5 (590–725), which is the
advertisement panel on the right, not the menu text at roughly x 300–500. LF2's menu
entries are selected with the player-1 controls from `data/control.txt`, which default to
the numpad.

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
| `LF2_RSRC_DEBUG` | log bitmap loads, `StretchBlt` calls and resource lookups |
| `LF2_STR_DEBUG` | log `sprintf`/`fscanf`/`fopen` with their formats and results |
| `LF2_WATCH=<hex>` | report writes to a guest address; `LF2_WATCH_VAL` filters to one value |
| `LF2_FN_WATCH=<hex>` | on entry to that guest function, dump its caller, `ECX` and arguments |
| `LF2_WATCH_REL=<off>` | arm the memory watch on a slot in that function's frame |
| `LF2_ESP_LOG` | log ESP either side of every host call once the watched function is entered |
| `LF2_DUMP_SURF` | write each surface to `scratch/surf_NN.ppm` after a GDI blit |
| `LF2_COM_TRACE` + `tools/diff_trace.py` | compare the call sequence against a Wine oracle capture |

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
  LF2_AUTOKEY_ONCE=1 LF2_AUTOKEY=0x65,0x65,0x65,0x65,0x65,0x65,0x65,0x65,0x68,0x68,0x65 \
  LF2_AUTOKEY_START=32000 LF2_AUTOKEY_EVERY=1800 \
  ../scratch/build/lf2 lf2.exe
```

The click picks **game start**; the game then loads its data for ~25 s, which is why the
key script starts at 32 s and why clicks and keys need separate schedules
(`LF2_AUTOCLICK_START`/`_EVERY` fall back to the `LF2_AUTOKEY_` ones). The eight attacks
walk mode select, player join and the computer-player count; the two ups move from
"Reset Random" to **Fight!** on the pre-fight overlay, and the last attack starts the
match. Both scripts need their `_ONCE` flag: cycling walks straight back out again.

## The menu map, and how it was recovered

`403,228` is **game start**. Clicking it loads the game data (`Now Loading… data\*.dat`,
about 25 s) and reaches the mode menu — VS mode, Stage mode, Championships, Battle mode,
Demo, Playback, Quit. Player 1's keys then drive it: up is Keypad 8 (`0x68`) and attack is
Keypad 5 (`0x65`), which the control settings page states outright. From there it reaches
Character Selection and the Battle-mode team setup.

Main menu bands, from `tools/click_bands.py`:

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

- `tools/click_bands.py` — recovers clickable bands from the game's own comparison
  constants. Only comparisons against the register the mouse coordinate was loaded into
  count; bounds stay in source order, since sorting destroys the lo/hi pairing. An x pair
  may be followed by several y pairs (one menu, several entries).
- `tools/find_path.py` — greedily extends a click path, keeping any candidate that yields
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

The port sits at roughly **13% of one core** during play. It was 96% until `Sleep` was
implemented: the import was mapped to a no-op that returned immediately, so the game's
frame pacing — a `Sleep` in a loop — became a spin. Honouring it is also the faithful
behaviour, since on Windows it blocks the thread and the game is written expecting that.

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
cd scratch/build && ctest           # everything, including the ~75 s smoke test
cd scratch/build && ctest -LE slow  # the fast set only
```

`tools/smoke_test.sh` drives the port deep into the game headless and asserts what has
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

**Is 10 s slow?** Not obviously. Wine running the same binary on the same machine takes
about the same order of time to get past its main menu after the equivalent click (screen
content stops changing at ~11 s, measured by perceptual hash of the framebuffer). That
comparison is coarse — a hash cannot tell the loading screen from the mode menu, so no
precise ratio should be read into it — but it is enough to say the recompiled code is not
grossly slower than the original. There is no obvious win left here, which is why the
investigation stopped.

The obvious follow-up did **not** pay off, which is worth recording so it is not tried
again: `h_fscanf` called `getenv` on every one of those 2.5M invocations, and caching it
changed the load time by nothing measurable (10.1 s / 10.3 s against 10.2 s). glibc's
`getenv` is cheap next to the surrounding parse. The caching was kept because it is the
right shape for a flag on a hot path, not because it bought anything.

## Playing with a controller

Plug a pad in and play. Nothing to configure, and the keyboard keeps working at the same
time.

| control | effect |
|---|---|
| d-pad / left stick | the player's directions; in the front-end menu, moves the selection |
| A (south) | attack; in the front-end menu, activates the selection |
| B / X | jump / defend |
| Start | activates the front-end menu's selection |

Pads are handed to player slots in order, so a second controller is player two with no
configuration either.

### Why this needed a port rather than a shim

`joyGetPosEx` and friends are reimplemented on SDL3 in `runtime/gamepad.c`, and they were
answering correctly long before a controller did anything useful. The reason nothing
happened is one level up: **a controller reaches a player only if that player's control
config names a joystick**, and nothing sets that without a trip to the settings screen.

That decision lives in `fn_00419a60`, the per-frame input gather, so that is what is
ported (`runtime/overrides.c`). Read out of the original:

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
game's own hit-test constants (`tools/click_bands.py`).

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
- **the whole route** — `tools/controller_test.sh` (ctest target `controller`) drives from
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

`fn_004246b0` (the menu) is being ported incrementally in `runtime/overrides.c`. What is
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

A controller drives it directly (`runtime/gamepad.c`), and a mouse still works as before.
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
port were indistinguishable from the outside — which is why `tools/controller_test.sh`
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

## Finding which code draws something

Two hooks in `runtime/ddraw.c`, both of which were needed to port the ads out:

- **`LF2_BLT_RECTS=1`** logs every blit destination rectangle. That is how the ad regions
  were enumerated: top strip `(0,0)-(397,34)`, right panel `(590,199)-(788,393)`, its arrows,
  and the bottom row — against the character art at `(0,0)-(330,546)`.
- **`LF2_BLT_STACK=<x>,<y>`** walks the guest stack at the first blit landing on that
  destination and prints everything that looks like a `.text` address. It is a rough
  backtrace, not an exact one, but it is enough to **diff two chains**.

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
