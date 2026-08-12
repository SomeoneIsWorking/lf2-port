---
id: 62
title: Stage mode: hand-weave the levels in 3D so HD2D has real depth to light
status: open
symptom: Stage mode's levels are flat 2D layers, so the HD2D pass has nothing to light: the depth it needs is not in bg.dat and cannot be derived from it. The ask is per-stage 3D geometry, authored by hand, that the levels are woven into.
tags: reported,renderer,hd2d,stage
created: 2026-08-12
updated: 2026-08-12
---

## What was reported

> also stage mode 3D hand-weave levels for better HD2D

A feature request, and the largest one in the queue. HD2D means sprites in a lit 3D set; this
port has the sprites and (issue #49) not yet the set.

## Why the current renderer cannot get there on its own

`runtime/video/hd2d.c` lights a composition that is, geometrically, a stack of flat layers.
`bg.dat` gives each layer a picture, an x, a y, a scroll `width:` and a `loop:` -- a parallax
recipe, not a scene. `zboundary:` gives the walkable band's two rows and nothing else. There is
no wall, no floor plane, no height for anything, and none of it is recoverable from the data
file: the depth simply was never authored, because LF2 never needed it.

So this cannot be an RE task. There is no original mechanism to port -- which makes it the one
kind of work in this repo where inventing is correct, and it must be labelled as such rather
than dressed up as a finding.

## What "hand-weave" has to mean concretely

Per stage, authored geometry that the existing 2D layers are mapped onto:

- a **floor plane** through the `zboundary:` band, which is the one piece the data DOES pin --
  LF2's depth axis projects straight down the screen, so the band's two rows are the floor's
  near and far edges and the mapping from a fighter's z to a world position is already known.
- **billboard depth per layer**: each bg.dat layer placed at a z, so parallax comes out of the
  camera rather than out of `width:`. The existing `width:` is the constraint that has to be
  reproduced, not discarded -- a layer placed at the wrong z scrolls wrong, and that is
  measurable against the current renderer.
- **hand-authored solids** where a stage has them: the pillars in the throne room, the walls of
  the cave. This is the part that is genuinely new art data.

## Constraints this must respect

- **It is a stage-mode-first feature but not a stage-mode-only one**: VS stages use the same
  bg.dat records, so whatever describes the geometry has to sit beside a background record
  rather than inside stage mode's logic.
- **The data lives in the repo, the ART does not.** Geometry authored here is the port's own
  work and is committable; anything derived from the shipped binary or the game's own files is
  not.
- **The software compositor must keep working.** It has no depth buffer and never will, so the
  geometry has to be additive -- a stage with no authored geometry draws exactly as it does
  today, and `tools/e2e.sh background` stays byte-exact.

## Dependency

Issue #49 is the prerequisite and says so: its first commit is a depth buffer, and there is
nothing to weave levels into until the renderer can express depth at all. This entry is the
thing #49 is for.

## Open, and needs a decision before any code

Where the geometry is authored and in what format -- a per-stage file beside `bg.dat`, a single
table, or something generated from a tool -- is not decided. Nor is whether the first stage is
done by hand end to end (to find out what the format needs) or the format designed first.
