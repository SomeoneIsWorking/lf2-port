# Project state

## Comparison baseline

The baseline is the unmodified Windows release of *Little Fighter 2 v2.0a* running on Windows or
through Wine, with its fixed-resolution 4:3 DirectDraw presentation, original keyboard/joystick
configuration, and manual game-file setup. The port's intended differences are native execution,
modern display effects and aspect ratios, remappable multi-device controls, and packaged setup.

## Current focus

S004 is the current focus.

## Capability inventory

| ID | Capability or outcome | State | Factual dependency | Goals |
| --- | --- | --- | --- | --- |
| S001 | The native port boots, navigates menus, and plays VS and Stage Mode with audio | verified | — | G001 |
| S002 | High-resolution rendering preserves the game's pixel-art presentation | verified | S001 | G001 |
| S003 | Keyboard and controller actions are remappable and persist across runs | partial | S001 | G001 |
| S004 | The Linux AppImage provides no-terminal first-run game-file setup | partial | S001 | G001 |
| S005 | The x86 game and Windows platform calls run through native static recompilation and SDL owners | verified | S001 | G001 |
| S006 | Widescreen and ultrawide expand the visible stage instead of stretching the 4:3 picture | verified | S002 | G001 |
| S007 | Native character lighting and cast shadows can be configured in the port menu | verified | S002 | G001 |
| S008 | Two physical controllers can join as two local players without manual slot setup | partial | S003 | G001 |
| S009 | Controllers can connect, disconnect, and reconnect without restarting the game | partial | S003 | G001 |
| S010 | Borderless, windowed, fullscreen, and Alt+Enter display switching work | verified | S002 | G001 |
| S011 | Menus and character selection use modern anti-aliased text | verified | S001 | G001 |
| S012 | The macOS native build and Metal renderer are release-qualified | partial | S001, S002 | G001 |
| S013 | The Android ARM64 build provides touch controls and private installer/folder/ZIP setup | partial | S001, S003 | G001 |
| S014 | Network play from the original game is available natively | missing | S001 | G001 |

## Capability details

### S001 — Playable game flow

Evidence: documented shipping routes cover boot, menus, character selection, VS matches, Stage Mode,
sound effects, and WMA background music.

### S002 — High-resolution presentation

Evidence: the running product renders at modern output resolutions while keeping sprites
nearest-filtered and preserving their authored pixel-art character.

### S003 — Persistent input bindings

The production input path and RmlUi port menu implement keyboard and controller navigation plus
persistent action bindings.

Gap: representative physical-controller behavior still needs end-to-end verification on shipping
hardware.

### S004 — Linux AppImage setup

Linux AppImage setup accepts the original installer, extracted tree, or bounded ZIP through a native
first-run dialog.

Gap: the complete clean-machine AppImage install-and-play path lacks a recorded release gate.

### S005 — Native host boundary

Evidence: the game executable is translated from x86 to C and its Win32, DirectDraw, DirectSound,
GDI, and input boundaries are implemented through native SDL-backed owners.

### S006 — Widescreen and ultrawide

Evidence: captured 16:9 and ultrawide matches show additional stage area rather than a horizontally
stretched 4:3 image.

### S007 — Lighting and shadows

Evidence: the native renderer and port menu expose character lighting and cast-shadow controls in the
running product.

### S008 — Two-controller local play

The input owner supports four controller slots and automatically assigns a second pad to Player 2.

Gap: the two-controller route needs a current end-to-end physical-hardware verification run.

### S009 — Controller hot-plug

Connect, disconnect, and reconnect handling is implemented through stable native device ownership.

Gap: the lifecycle has not been verified with representative physical controllers.

### S010 — Window modes

Evidence: borderless, windowed, fullscreen, and Alt+Enter switching are implemented in the shipping
display path.

### S011 — Anti-aliased text

Evidence: menu and character-selection text uses SDL_ttf with redistributable embedded fonts.

### S012 — macOS release

The native macOS build has been produced and Metal shader support was added.

Gap: the Metal rendering change still needs the user acceptance run recorded by issue #100.

### S013 — Android release and touch controls

The ARM64 build, landscape policy, private installer/folder/ZIP setup, all-menu touch routing,
updater, and signed-build checks are implemented.

Gap: signed installation, device correctness, and performance remain unverified on named devices.

### S014 — Network play

Missing capability: the original network mode is not ported and currently reports that no network is
available.
