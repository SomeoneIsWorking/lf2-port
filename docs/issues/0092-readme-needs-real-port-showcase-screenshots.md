---
id: 92
title: README needs real port showcase screenshots
status: resolved
symptom: The README screenshot gallery shows only isolated port UI and does not visibly demonstrate widescreen gameplay or the native renderer.
tags: reported,docs,readme,screenshots,rendering
created: 2026-08-22
updated: 2026-08-22
---

The README currently has two port-owned UI captures on black backgrounds, which do not showcase the actual LF2 port. Add representative in-game screenshots, including widescreen gameplay and a port overlay/settings surface. The user explicitly clarified that game screenshots are acceptable. Keep the exception narrow: promotional screenshots are allowed; extracted assets, executables, and raw game content remain excluded.

### Resolution (2026-08-22)
Replaced the isolated UI-only gallery with fresh deterministic 1920x1080 captures of active demo combat, the global port menu, renderer/lighting controls, and keyboard/controller bindings. README now explains the promotional screenshot exception, and AGENTS.md keeps that exception narrow while continuing to forbid extracted assets and reconstructable game content. All 26 offline tests pass.
