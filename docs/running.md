# Running the port

```sh
cmake -S . -B build && cmake --build build -j
cd game && ../build/lf2 lf2.exe
```

The working directory must be the extracted game tree — the game opens its data with
relative paths.

## Headless runs and screenshots

Forcing the video driver is required, and the reason is worth knowing:

```sh
Xvfb :99 -screen 0 1024x768x24 &
cd game && DISPLAY=:99 SDL_VIDEODRIVER=x11 ../build/lf2 lf2.exe &
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

**But those messages never leave the queue.** The dispatch log shows only the startup
messages and thousands of empty ones, so `push_message` runs and `next_queued_message`
does not hand the result back. That is a defect in the port's own message plumbing, not
timing and not the game — and it is the next thing to fix.

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
cmake -S . -B build -DLF2_STACK_CHECK=ON   # assert stack balance at every guest RET
LF2_PROBE=4246fd,4274da cmake --build build --target lf2   # log ESP at those instructions
```

`LF2_PROBE` is read when the code is **generated**, not when it runs, so the target has to
be rebuilt after changing it.
