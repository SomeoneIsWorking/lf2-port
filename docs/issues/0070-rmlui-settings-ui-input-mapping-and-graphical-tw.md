---
id: 70
title: RmlUi settings UI: input mapping and graphical tweaks
status: closed
symptom: The keyboard layout is one hardcoded set (arrows+Z/X/C in runtime/overrides/input.c) with no way to remap, and the renderer/light/DOF options live in a hand-rolled pause-menu page that cannot grow a real settings tree
tags: reported,feature,ui,rmlui,input,options
created: 2026-08-16
updated: 2026-09-09
---

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

### Note (2026-08-16)
RmlUi is no longer the requirement. The user's follow-up: 'You can make a regular UI, using game's systems or a custom UI for device mapping'. So the device-mapping screen is a REGULAR UI on the port's own systems -- the pause-menu page (runtime/app/pause.c) that already takes keyboard/pad/mouse and draws with the game's own glyphs, or a custom UI -- not a C++ library dependency. RmlUi is off the table unless a genuine document tree appears. The graphical tweaks are already the pause-menu Options page (#69), not RmlUi.

### Note (2026-08-20)
USER 2026-08-20 explicitly restored the RmlUi requirement: 'Add RmlUi with input mapping'. This supersedes the 2026-08-16 note that RmlUi was off the table. RmlUi plus persistent keyboard/controller mapping is the required UI, not the hand-rolled substitute.

### Reopened (2026-08-20)
USER 2026-08-20 reports the shipped RmlUi is completely broken and is only reachable by opening the legacy Escape menu first. Reproduce through the default launcher; repair rendering and mapped input through the real shipping path, and do not count a scripted texture-load assertion as proof of usability.

### Reopened (2026-08-21)
USER 2026-08-21: RmlUi should be controllable via any input device, including keyboard and controller. Re-audit the real navigation path across all connected pads/keyboards; mapped navigation in one scripted path is not sufficient evidence.
