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

Mouse input is now implemented and **demonstrably reaches the game**: sending
`WM_MOUSEMOVE` makes it redraw its cursor at the new position, which it did not do before.
The game imports no `GetCursorPos`, so that lParam is the only way it can learn where the
pointer is.

Selection still does not fire. The cursor also lands lower than the coordinate asked for,
so the window-to-game coordinate mapping is suspect — `SDL_RenderCoordinatesFromWindow`
accounts for letterboxing, but the game renders into a 794x550 surface that is itself
being presented into a window of a different aspect. Getting that mapping exactly right
is the next step, since a click that lands on the wrong row would look exactly like a
click that does nothing.

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
