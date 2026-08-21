---
id: 85
title: Held crate appears transparent over character hair
status: resolved
symptom: While a fighter carries a wooden crate overhead, patches of the fighter's hair appear inside the lower face of the crate as if the box were transparent
tags: reported,rendering,sprite,weapon,color-key,draw-order
created: 2026-08-21
updated: 2026-08-21
---

## Reported

USER 2026-08-21: "is this box transparent or something? why is his hair showing through it?"

The supplied screenshot shows hair-coloured pixels within the lower-left/front face of a held wooden crate. Determine whether those pixels are authored in the crate frame, exposed by its colour key, drawn by the fighter after the crate, or caused by stale/wrong texture content.

## Root cause

The crate bitmap is opaque at the reported pixels. `data/weapon3.dat` maps the carried
wooden box to `frame: 10` / `pic: 5`; extracting that cell with the sheet's 59-pixel frame
pitch shows uninterrupted wood across its front face.

Davis's heavy-object frames 12–19 all pair `weaponact: 10` with `cover: 0`. That is the
original game's explicit ordering: draw the held weapon, then draw Davis over it so his hands
can wrap around the object. His hair is also part of the later fighter sprite, so it overlaps
the crate along with the intended hands. The observed pixels are not alpha, an incorrect
colour key, stale texture contents, or an invisible part of the crate.

## Dead ends

An initial crop appeared to contain fighter pixels because it treated `w: 58` as the cell
pitch. LF2 sprite sheets include the separator column, so the pitch is 59. The mirrored half
of the sheet also uses whole-sheet coordinates rather than a second local origin.

## Resolution

No port change. Preserving the authored `cover: 0` order is the faithful result; changing it
would put the whole crate over Davis and hide the hands the frame was authored to expose.
