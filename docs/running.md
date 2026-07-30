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

`LF2_READ_WATCH=<lo>:<hi>` reports which offsets inside a span the game loads, by novelty:
a sweep closes when an offset repeats, and a set is printed only when it differs from the
previous sweep. A malformed or empty span is refused with exit 2 rather than silently
watching nothing, and the hit count is reported even when it is zero, so "saw nothing" is
distinguishable from "was never armed". Disabled it costs one predictable not-taken branch
per load; measured frame throughput is unchanged (~30 fps either way).

**What it showed about input, including the part that did not work.** The intent was to
recover a per-screen input signature by watching the key array at `0x455378`. That does not
work: the game sweeps the entire array linearly, offsets `00` through `f9`, every frame to
rebuild its input bitmask. Selective checks are buried inside that bulk scan, so watching
by *address* cannot separate "the game is asking about the Up key" from "the game is
rebuilding its bitmask".

Separating them needs the reading instruction's address, not the read address — the bulk
scan is one call site and the selective checks are others. That is not currently
recoverable, because `cpu.eip` is only maintained at call boundaries in the generated code.
Recorded so the next attempt starts from there instead of re-deriving it.
