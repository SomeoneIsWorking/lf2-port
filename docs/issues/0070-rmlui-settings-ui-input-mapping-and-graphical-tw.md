---
id: 70
title: RmlUi settings UI: input mapping and graphical tweaks
status: resolved
symptom: The keyboard layout is one hardcoded set (arrows+Z/X/C in runtime/overrides/input.c) with no way to remap, and the renderer/light/DOF options live in a hand-rolled pause-menu page that cannot grow a real settings tree
tags: reported,feature,ui,rmlui,input,options
created: 2026-08-16
updated: 2026-08-21
---

## Reported

The user asked for RmlUi to host input mapping (control remapping) and graphical tweaks
(renderer and its effects). This reverses the earlier decline recorded in runtime/app/pause.c
and docs/running.md, which said RmlUi was wrong for 'two numbers' but should be revisited 'if
the port ever grows a real settings screen'. Input mapping plus a settings tree is exactly
that screen.

## The constraint

- The build is deliberately 'a C compiler and SDL and nothing else' (docs/codemap.md). RmlUi
  is C++ with its own build, font stack and render backend; adding it makes it the largest
  dependency in the port. This must be an explicit, documented decision, not an accident.
- There is NO config file in the port yet ('inventing one for two numbers is the wrong order'
  -- docs/running.md). A settings screen implies persistence, so a config file lands with it.
- runtime/overrides/input.c hardcodes the one keyboard layout (VKS[7]) and deliberately
  removes the game's own control.txt layouts. Input mapping means that table becomes
  read-from-config, and every consumer of the seven buttons goes through it.
- The graphical options already exist as runtime/app/options.{c,h} (renderer/lighting/DOF),
  owned by the pause menu. RmlUi would own them instead, so options.{c,h} becomes the shared
  state the RmlUi document binds to.
- Dusklight is the prior art to crib from (src/dusk/ui), per the pause.c note.

## Open questions that decide the shape

- Vendor RmlUi how (submodule, FetchContent, vendored snapshot)? The repo has no vendor/ dir.
- RmlUi renders through its own render interface; it must draw into the port's SDL3_GPU
  engine (or the SDL_Render classic path) over the frozen frame like the pause menu does now.
- Which input devices does mapping cover (keyboard only, or pad buttons too)?

### Note (2026-08-16)
RmlUi is no longer the requirement. The user's follow-up: 'You can make a regular UI, using game's systems or a custom UI for device mapping'. So the device-mapping screen is a REGULAR UI on the port's own systems -- the pause-menu page (runtime/app/pause.c) that already takes keyboard/pad/mouse and draws with the game's own glyphs, or a custom UI -- not a C++ library dependency. RmlUi is off the table unless a genuine document tree appears. The graphical tweaks are already the pause-menu Options page (#69), not RmlUi.

### Note (2026-08-20)
USER 2026-08-20 explicitly restored the RmlUi requirement: 'Add RmlUi with input mapping'. This supersedes the 2026-08-16 note that RmlUi was off the table. RmlUi plus persistent keyboard/controller mapping is the required UI, not the hand-rolled substitute.

### Note (2026-08-20)
USER 2026-08-20: copy the RmlUi integration from Dusklight. The LF2 implementation must follow Dusklight's current UI ownership/backend/input pattern rather than growing a bespoke monolithic glue file.

### Resolution (2026-08-20)
RmlUi now owns a persistent keyboard/controller mapper and graphical settings document. Its SDL backend is a separate UI module, controller capture is release-gated, the shipping input gather consumes the mappings, ctest bindings passes, and the settings route opens and renders the real document.

### Reopened (2026-08-20)
USER 2026-08-20 reports the shipped RmlUi is completely broken and is only reachable by opening the legacy Escape menu first. Reproduce through the default launcher; repair rendering and mapped input through the real shipping path, and do not count a scripted texture-load assertion as proof of usability.

### Resolution (2026-08-21)
RmlUi was broken for three independent integration reasons: SDL_UpdateTexture's SDL3 bool result was inverted so successful font-atlas uploads were destroyed; the backend leaked viewport/clip state into the shared renderer; and the document used browser-like bindings unsupported by RmlUi. Replaced the legacy pause painter with one global Dusklight-structured RmlUi document, corrected atlas/upload and render-state ownership, used RmlUi data-model syntax and tabbable controls, blocked guest input while modal, and verified mapped pad navigation plus rendering through settings and ui_global routes.

### Reopened (2026-08-21)
USER 2026-08-21: RmlUi should be controllable via any input device, including keyboard and controller. Re-audit the real navigation path across all connected pads/keyboards; mapped navigation in one scripted path is not sufficient evidence.

### Resolution (2026-08-21)
The settings document had bespoke two-pad polling, no mapped keyboard-action translation, manual controller clicks, and no directional repeat. A separate Dusklight-style runtime/ui/rmlui_input.cpp now merges keyboard and all attached-controller action state, latches short event edges, applies accelerated directional repeat, maps Attack/Jump to Confirm/Cancel, and keeps conventional raw controls when unbound. The physical keyboard route activates Continue through mapped Attack; the controller route navigates to and activates Controls.
