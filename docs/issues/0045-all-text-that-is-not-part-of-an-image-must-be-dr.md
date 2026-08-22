---
id: 45
title: All text that is not part of an image must be drawn from a vector font shipped in the repo
status: resolved
symptom: text is rasterised at the composition's resolution and then scaled with everything else, so it is the one thing in the frame that does not get sharper as the window grows
tags: reported,rendering,text,gdi,scaling,feature
created: 2026-08-11
updated: 2026-08-21
---

REPORTED 2026-08-11. Filed on receipt. The reporter's words: all text that is not part of an
image needs to use SVG fonts, placed in the project.

WHAT THIS IS THE OTHER HALF OF. Issue #41 made the picture scale with the window by scaling
every quad as it is drawn, so the geometry is exact and only a sprite's texels are magnified.
Text was left behind and it was stated as the remaining half: runtime/win32/gdi.c rasterises a
string at the COMPOSITION's resolution and the result is then scaled up with everything else,
so a 1080-row window shows a magnified bitmap of a small glyph. A vector font rasterised at
the FINAL size is what "render at a higher res" actually means for text.

WHICH TEXT THIS IS, and the distinction matters because most of what looks like text in LF2 is
not: the logo, the menu labels on the front end, the fighter names on character selection and
much of the HUD are BITMAP ART inside the game's sprite sheets. Those are images and are out
of scope -- they scale as sprites and nothing here changes them. What IS in scope is the text
the game draws through GDI (h_TextOutA in runtime/win32/gdi.c and the paths around it), which is
where the game's own dynamic strings go.

FIRST TASK, before any font work: enumerate what actually goes through the GDI path in a real
run and where. runtime/win32/gdi.c already has a hook that logs every string with its position (see
the comment about the leftover "Update" string). That list decides the size of this job, and
it may be short.

CONSTRAINTS THAT ARE NOT NEGOTIABLE:
  - THE FONT MUST BE REDISTRIBUTABLE AND COMMITTED. The repo ships no game content; a font is
    not game content, but it must be one whose licence allows redistribution (OFL or similar)
    and its licence file goes in with it. The build must not depend on a system font: SDL3_ttf
    is already an optional dependency and the port falls back to SDL's built-in debug font
    when it is absent, which is exactly the kind of silent difference that should not decide
    what the player sees.
  - IT MUST BE RASTERISED AT THE FINAL SIZE, not at 794x550 and scaled. The text path needs
    the world scale (lf2_world_scale, issue #41) where it rasterises, and the glyph cache has
    to be keyed on the size it was rasterised at or a resize keeps showing the old one.
  - THE GAME'S OWN LAYOUT DECIDES POSITION. The game passes x/y to TextOutA in its own screen
    coordinates; those are the anchor. A vector font with different metrics will not have the
    same advance widths, so a string the game positioned to fit a panel can overflow it. That
    has to be measured against the real screens, not assumed.

NOT ESTABLISHED: whether "SVG font" means the SVG-font format specifically (deprecated, and
essentially unsupported by rasterisers today) or simply a vector/outline font -- TTF/OTF,
which is what SDL3_ttf consumes. Treat it as "a scalable outline font shipped in the repo"
unless the reporter means the literal format, and ASK if the distinction turns out to matter.

### Resolution (2026-08-11)
Both text paths -- the GDI TextOutA one and the game's own 8x16 bitmap-sheet one, which is the larger -- now draw from Liberation Sans/Mono committed in assets/fonts/ under SIL OFL 1.1 and embedded as byte arrays at configure time. The system-font search and the silent debug-font fallback are deleted and SDL3_ttf is required. Glyphs are rasterised at the window's resolution via a tile whose pixel size is separate from its placement, with size-keyed caches. The original 1920x1080 evidence -- Sans opened at 26pt, 23 glyphs requested at 1.96x, and an 88.7% difference from a tile-less frame -- proved that tiles existed, not that the engine retained those pixels: a composition-sized target could downsample them and enlarge the result while satisfying every one of those observations.

### Reopened (2026-08-21)
USER 2026-08-21: 'fonts are still low res'. The supplied screenshots are the game's pre-fight overlay and in-match HUD, not RmlUi. The dynamic values and HUD text use the fixed-cell font-sheet path in runtime/win32/gdi.c; the overlay's large labels are image-authored and are tracked separately in #84. Verification must exercise the shipping renderer rather than only report a requested raster scale.

### Resolution (2026-08-21)
The high-resolution glyph rasters still existed, but both native renderers sampled every tile as pixel art: NEAREST re-quantized host outline-font coverage at fractional output scales. Host ARGB glyph/SVG tiles now use linear sampling while guest sprites remain nearest. The simulated 200% route exercises the shipping engine, reports 23 game glyphs rasterized at 2.00x with zero cache drops, and now also requires the engine's own target report to be the 1588x1100 drawable. That last assertion is the discriminator the earlier proof lacked: a requested 2x tile followed by a composition-target downsample no longer passes. Image-authored labels are separately tracked in #84.
