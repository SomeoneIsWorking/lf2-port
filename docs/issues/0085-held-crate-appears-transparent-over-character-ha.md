---
id: 85
title: Held crate appears transparent over character hair
status: closed
symptom: While a fighter carries a wooden crate overhead, patches of the fighter's hair appear inside the lower face of the crate as if the box were transparent
tags: reported,rendering,sprite,weapon,color-key,draw-order
created: 2026-08-21
updated: 2026-09-09
---

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

### Reopened (2026-08-22)
USER 2026-08-22: hair is again visible through a solid carried boulder, and the user explicitly identifies it as the same bug previously reported with the crate. The prior resolution treated the game's authored cover:0 ordering as acceptable, but the observed solid-object occlusion is not acceptable for this port. Reopen and replace that conclusion with a general solid-held-object occlusion fix that covers both crates and boulders without per-asset pixel suppression.
