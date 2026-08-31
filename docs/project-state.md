# Project state

## Current focus

S004 is the current focus.

## Capability inventory

| ID | Capability or outcome | State | Factual dependency | Goals |
| --- | --- | --- | --- | --- |
| S001 | The native port boots, navigates menus, and plays VS and Stage Mode with audio | verified | — | G001 |
| S002 | The renderer provides high-resolution, widescreen, ultrawide, lighting, and shadows | verified | S001 | G001 |
| S003 | Persistent keyboard and controller mappings support local multiplayer and hot-plug policy | partial | S001 | G001 |
| S004 | Linux, macOS, and Android releases provide validated no-terminal game setup | partial | S001 | G001 |
| S005 | The x86 game and Windows platform calls run through native static recompilation and SDL owners | verified | S001 | G001 |

## Capability details

### S001 — Playable game flow

Evidence: documented shipping routes cover boot, menus, character selection, VS matches, Stage Mode,
sound effects, and WMA background music.

### S002 — Native presentation

Evidence: the running product has captured high-resolution 16:9 and ultrawide gameplay with additional
stage area, nearest-filtered sprites, character lighting, and cast shadows.

### S003 — Input and multiplayer

The production input path implements keyboard and controller navigation, persistent bindings, two-pad
play, four controller slots, and connect/disconnect handling.

Gap: representative physical-controller hot-plug and multiplayer behavior still needs end-to-end
verification on shipping hardware.

### S004 — Packaged releases

Linux AppImage setup accepts the original installer, extracted tree, or bounded ZIP. Android ARM64
setup, touch routing, and updating are implemented.

Gap: macOS rendering needs a user re-test and Android still needs release qualification on named
devices.

### S005 — Native host boundary

Evidence: the game executable is translated from x86 to C and its Win32, DirectDraw, DirectSound,
GDI, and input boundaries are implemented through native SDL-backed owners.
