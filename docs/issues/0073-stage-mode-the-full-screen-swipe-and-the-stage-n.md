---
id: 73
title: Stage mode: the full-screen swipe and the STAGE n-n announcement are anchored to 794
status: resolved
symptom: At a wide window the stage-intro full-screen swipe transition and the 'STAGE n-n' announcement banner are laid out for the 794-wide screen and do not follow the wide view
tags: reported,widescreen,stage-mode
created: 2026-08-16
updated: 2026-08-17
---

## Reported

The user asked for the stage-mode fullscreen swipe and the stage number announcement to be
adjusted to widescreen. This is DISTINCT from issue #54 (the persistent Man/HP/STAGE-n-n UI
row, already resolved via a GDI text band).

## The constraint

- Issue #54 settled the persistent status row. The 'STAGE n-n' ANNOUNCEMENT here is the
  intro banner that swipes at the start of a stage, a different draw with (probably) a
  different path -- do not assume it is the same GDI text.
- The full-screen swipe is likely a game-owned full-screen fill/blit transition; it needs to
  span the wide composition like the other full-screen fills do (docs/running.md, issue #42),
  i.e. the flat colour is extended while centred art is not stretched.

## To characterize

- Find which function draws the stage-intro swipe and the announcement (likely in the object
  pass fn_0041a5a0 or the stage-draw path fn_0043f010), via LF2_DRAW_PATHS / LF2_TEXT_DEBUG /
  LF2_BLT_RECTS, before touching a constant. The fix must use screen_offset_x() / the same
  framing the other screen furniture takes, not a new literal.

## Resolved

Both draws are the stage-mode intro in **`FUN_00437860`** (the world/status function the
game calls once per frame from `fn_0041bc90` at `0041f4bd`), which is NOT overridable -- it
is an ordinary guest function rather than a native override -- so the fix belongs in the
port's framing layer (`runtime/video/ddraw.c`), exactly where issue #42/#44 put the other
per-screen framing.

### The STAGE n-n ANNOUNCEMENT banner -- a BLIT, not GDI text

Distinct from issue #54's persistent row (which IS GDI text, handled by `hud_offset_x`).
The intro logo is a **bitmap banner** -- a sheet `794x600` -- drawn as keyed **blits** at
fixed 794 coordinates into the world band (y 299..340, below the HUD, above the ground fill
at y 356). `LF2_TEXT_DEBUG` shows only the persistent `STAGE 1-1` at y 110 (GDI, issue #54);
`LF2_BLT_FRAME` shows the banner as the only blits from the `794x600` sheet, landing at
`(265,299)-(441,340)` etc. Because it is a blit, `screen_offset_x()` does not reach it
either: it returns 0 during a match (`panel_hud_up()`), so the banner sat 184 px short of
the left edge at a 978 view -- the same mistake the name tags made before issue #55.

The fix centres it like the other fixed-794 screen furniture: shift its destination by
`(d->w - NATIVE_W)/2` (the same arithmetic as `screen_offset_x`, applied directly because
the world view suppresses that helper). Identified by the source sheet being the one
794x600 surface in the whole game AND the destination landing in the banner's band.

### The full-screen SWIPE -- the fill-widening gate

The swipe is `FUN_00437860`'s full-screen wipe: a series of **794-wide flat-colour fills**
(horizontal bands sweeping vertically, the intro/outro transition). These are
`FUN_00415160` fills, i.e. plain `COLORFILL` blits -- not the stage's tinted layers
(`world_band_hint`, already widened) and not the front-end backdrop.

`surf_Blt`'s full-screen-fill widening was gated on `!panel_hud_up()` -- written for the
front-end/menu backdrops (issue #42/#44). During a MATCH that gate declined, so the swipe
stayed 794-wide and the stage it was supposed to cover showed past its edge on a wide view.
The gate is removed: a fill that spans the game's whole 794 screen is a full-screen
backdrop/wipe regardless of whether the HUD is up, and is widened to the composition. On
the frames the gate was written for it never mattered (`compose_off` is zero during a
match, and the stage's own bands are already widened by `world_band_hint`), so the only
fills this newly reaches are the swipe's; the full clear arrives already spanning the
surface.

### Verified

- `ctest` 13/13.
- `tools/e2e.py` background, objects, render, widescreen, resize, stage_mode, coop_dropin,
  smoke all pass (the stage fill and object byte-identity arms are unchanged at 794).
- The recorded Stage Mode scenario passed (stage reached, camera lock).
- At 794 the banner frames 628/630/632 are byte-identical to the pre-change build.
- Banner x: at 794 the logo spans 268..544 (centre 406); at 1100 it spans 421..697 (centre
  559 = 406 + (1100-794)/2); at 1920x1080 (978 composition) it spans 360..636 (centre 498 =
  406 + (978-794)/2). The banner keeps the same offset from the picture centre at every
  width. The swipe/ground fills span the full composition (y400 row reaches x977 at the
  978 view).
