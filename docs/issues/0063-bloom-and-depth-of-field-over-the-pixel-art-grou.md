---
id: 63
title: Bloom and depth of field over the pixel art, grounded in real depth rather than in a screen filter
status: open
symptom: Two thirds of issue #30's HD2D ask are delivered and one is not: the reporter asked for 'depth, lighting, bloom/DOF over the pixel art' and there is no bloom and no depth of field. A previous cut of both was removed because each touched every pixel and together they read as a filter over a screenshot.
tags: reported,renderer,hd2d,feature
created: 2026-08-12
updated: 2026-08-12
---

## What this is

Split out of issue #30 when the rest of it closed, so an explicit ask is not buried inside a
resolved entry. The reporter's words were "an HD2D look (depth, lighting, bloom/DOF over the
pixel art)". The lighting is built and the cast shadows are built; bloom and depth of field are
not, and this entry owns them.

## Why the first attempt was removed, and what that constrains

Issue #30 records it: a bloom, a depth of field, atmospheric haze, a vignette and a colour grade
all shipped briefly and were all cut. Each touched every pixel, and together they read as a
filter over a screenshot rather than as a remaster. The bloom's bright pass was
`SDL_BLENDMODE_MOD` of the frame over itself, which is squaring rather than a threshold.

So the constraint is not "do it more carefully". It is that a **screen-wide effect with no
geometry under it has nothing to be right about** -- there is no answer to "is this blur
correct" when the blur is a function of screen position rather than of distance.

## What changed, and why this is now buildable rather than a repeat

Depth of field needs DEPTH, and the port now has it for real:

- every background layer's depth is derivable from its own parallax rate (claim C031) --
  0.89 for a foreground strip through 535 for a distant sky, on the shipped stages;
- an object's depth is its z (claim C018), which the game itself depth-sorts on;
- the mesh pass (`runtime/video/mesh.c`) has a real D32_FLOAT depth attachment.

A DOF built on those is defocus by DISTANCE, which is a physical statement that can be checked:
the fighters' plane is in focus and a layer at depth 4.66 is not, at every camera position. The
one that was cut could not make that claim.

## What must not be done

- No blur that is a function of screen position. Tilt-shift-by-row is the version of this that
  looks right on one screenshot of one stage and is wrong the moment the camera pans.
- No bloom whose bright pass is the frame multiplied by itself. A threshold is a threshold.
- Nothing that changes a frame with no fighters and no stage geometry in it. `tools/e2e.sh
  render` has that arm precisely because it is what catches an effect spreading over the whole
  picture, and it must stay green.

## Depends on

Issue #62. Not strictly -- a DOF could be driven from the layer depths alone, since those are
already derivable without any of #62's authored geometry -- but the acceptance test is much
better once there is geometry at known depths to be in and out of focus.
