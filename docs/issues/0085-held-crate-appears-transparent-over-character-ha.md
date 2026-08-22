---
id: 85
title: Held crate appears transparent over character hair
status: resolved
symptom: While a fighter carries a wooden crate overhead, patches of the fighter's hair appear inside the lower face of the crate as if the box were transparent
tags: reported,rendering,sprite,weapon,color-key,draw-order
created: 2026-08-21
updated: 2026-08-22
---

## Reported

USER 2026-08-21: "is this box transparent or something? why is his hair showing through it?"

The supplied screenshot shows hair-coloured pixels within the lower-left/front face of a held wooden crate. Determine whether those pixels are authored in the crate frame, exposed by its colour key, drawn by the fighter after the crate, or caused by stale/wrong texture content.

## Root cause

The crate bitmap is opaque at the reported pixels. `data/weapon3.dat` maps the carried wooden
box to `frame: 10` / `pic: 5`; extracting that cell with the sheet's 59-pixel frame pitch shows
uninterrupted wood across its front face.

The previous ordering conclusion was backwards. The instruction listing at
`0x00418310..0x0041849a` shows that a carrier wpoint with `cover: 0` copies the carrier's z to
the held weapon and then adds one. `fn_0041a5a0` sorts objects by ascending z and paints them in
that order, so the original game deliberately paints the held crate or boulder *after* the
carrier. Its solid texels therefore cover hair while keyed holes still expose the hands. The
guest ordering was already correct and needed no asset or object-type exception.

The native lighting chain broke that result after the colour pass. It independently redrew
every grounded character silhouette into `tex_chars`, without checking whether a later painter
had covered the same output pixel. Hidden hair therefore remained in the character mask and the
light pass applied character lighting through that shape onto the already-painted crate or
boulder. The colour pass also blended zero-alpha keyed texels away while still letting them
write depth, so its depth target described sprite rectangles rather than final visible texels
and could not safely answer the mask's visibility question.

## Dead ends

An initial crop appeared to contain fighter pixels because it treated `w: 58` as the cell
pitch. LF2 sprite sheets include the separator column, so the pitch is 59. The mirrored half
of the sheet also uses whole-sheet coordinates rather than a second local origin.

## Implementation

The sprite fragment shader now discards fully transparent keyed fragments before depth writes.
The character-mask pipeline reloads the completed scene D32 target, carries the exact same
painter ordinal as the corresponding colour quad, and tests `LESS_OR_EQUAL` without writing
depth. A character fragment at equal depth made the final scene and remains in the mask; a later
opaque weapon leaves a nearer depth and rejects the covered fragment. This is a general visible-
surface rule for every painter-ordered solid, not per-asset suppression.

`tests/test_painter_depth.c` exercises the production ordinal mapping and proves that later
painters are nearer while both ends remain inside the clip range. It does not pretend to test
SDL's fixed-function depth state.

The real GPU gate is `tools/e2e.py visibility`, backed by
`runtime/video/engine_visibility_probe.c`. It feeds a procedural host-ARGB character and a
later half-opaque, half-transparent occluder through the shipping `engine_draw`, then reads two
interior pixels back from the returned GPU texture. Its four arms require:

- unlit colour: opaque occluder on one half and exposed character on the other;
- character mask: covered on the opaque half and present through the transparent half;
- reversed painter order: character present on both halves; and
- final lighting: the solid occluder remains unchanged while the exposed character is
  materially relit.

The ordinary-order mask signature is deliberately neither uniform. Disabling the character
mask's depth test admits the opaque half; failing to load the completed depth loses the
equal-visible/later-covered relation; and removing the quad shader's transparent-fragment
discard rejects the transparent half. Every case destroys the required black/yellow signature,
while the reversed arm separately fixes the comparison direction. Thus the same real-GPU
readback falsifies every link and cannot be satisfied by a crate/boulder asset edit.

The combined Clang build passed all 28 offline tests, including shader freshness. The real-GPU
route then passed all four arms on 2026-08-22 with exact interior samples:

- unlit: `#2040c0 / #804020`;
- mask: `#000000 / #ffff00`;
- reversed mask: `#ffff00 / #ffff00`; and
- lit: `#2040c0 / #20110a`.

The opaque half therefore remains the authored occluder from colour through final lighting,
while the transparent half preserves and materially relights the character. Reversing painter
order restores the character mask across both halves.

### Reopened (2026-08-22)
USER 2026-08-22: hair is again visible through a solid carried boulder, and the user explicitly identifies it as the same bug previously reported with the crate. The prior resolution treated the game's authored cover:0 ordering as acceptable, but the observed solid-object occlusion is not acceptable for this port. Reopen and replace that conclusion with a general solid-held-object occlusion fix that covers both crates and boulders without per-asset pixel suppression.

### Resolution (2026-08-22)
The guest painter order was correct; the native lighting chain rebuilt hidden character silhouettes without testing completed scene visibility. The colour shader now discards transparent fragments before depth writes, and the character mask reloads completed D32 with the same painter ordinal and LESS_OR_EQUAL. The procedural shipping-engine GPU route passed unlit, normal/reversed character-mask, and lit arms, distinguishing both missing-depth and missing-discard regressions without asset-specific suppression.
