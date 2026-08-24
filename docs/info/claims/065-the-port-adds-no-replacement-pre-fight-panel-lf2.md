---
id: C065
kind: claim
status: holds
created: 2026-08-25
tags: rendering
depends: runtime/overrides/text.c, runtime/video/ddraw.c, runtime/video/host_frame.c
---

## Claim

The port adds no replacement pre-fight panel; LF2 CHARMENU bitmap art and pixel lettering are the sole panel presentation.

## Evidence

2026-08-25: issue #106 deleted overlay_panel, its host font and append hooks; a 1920x1080 forced-overlay capture visually shows the original pixel-authored Fight/Reset/Background/Difficulty/Exit panel with no rounded host panel.

## What would falsify it

A host panel producer or overlay-only font is reintroduced, or a forced-overlay capture shows host-authored layout/lettering over CHARMENU.
