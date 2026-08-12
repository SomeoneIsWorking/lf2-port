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

### Note (2026-08-12)
### Note (2026-08-12) -- the DEPTH this needs now exists and is measured. The blur does not yet.

The engine (issue #64) writes a second colour attachment carrying, per pixel, a surface normal
and the draw's real DISTANCE as a parallax depth. That is the input this entry says was missing.

THE TRAP THIS ENTRY WARNS ABOUT WAS NEARLY WALKED INTO ANYWAY, from a direction the entry does
not name. The engine already had a depth buffer -- but its depth is the draw's POSITION IN THE
PAINTER ORDER, which is the game's answer about what covers what. A defocus driven off that would
be "blur by how late something was drawn", which is a screen-space effect wearing a depth
buffer's clothes. Distance and draw order are two different quantities and the G-buffer keeps
them apart: the depth buffer orders, the G-buffer measures.

WHERE THE DISTANCE COMES FROM, and none of it is authored or guessed:
  layers    (stage_width - 794) / (span - 794), claim C031 -- the scroll rate IS a perspective
            divide written as a ratio, so it falls out of the shipped data
  geometry  the solid's own parallax depth, per vertex
  sprites   nothing. They write a distance of 0 and a normal of exactly (0,0,0), which is not a
            unit vector and so can never be a real one. That is the marker for "no surface here",
            and it is exact rather than a tolerance.

WHY A COLOUR ATTACHMENT AND NOT THE DEPTH BUFFER: SDL3 declares no pixel format for depth at all
(checked in SDL_pixels.h, not assumed), so a D32_FLOAT texture cannot be wrapped as an
SDL_Texture and nothing outside the engine could ever sample it. R16G16B16A16_FLOAT, because the
distance runs from about 0.89 to 535 on the shipped stages and eight bits of that is a staircase
a defocus would turn into banding by distance.

The G-buffer is NEVER blended, whatever the colour target does. Half way between a sprite at 1.0
and a sky at 535 is a plane nothing in the scene occupies.

MEASURED, not asserted -- LF2_ENGINE_GBUF=1 reads the buffer back (instrument I016) and reports
the distances actually in it, which are checked against the stage's own bg.dat:

    Brokeback Clif, 794x550     1.0000  210692 px   (its span-1500 layers)
                                1.2061  104808 px   (its span-1379 layers; C031 gives 706/585 =
                                                     1.2068, and 1.2061 is the nearest half)
                                        121200 px with no distance -- sprites, HUD, uncovered
    with a .stage authored      90 px carry a real surface normal
    with none                    0 px do

A COUNTER WOULD NOT HAVE DONE. It can only say the engine was HANDED a distance, not that the
distance survived the vertex format, the attachment, the half-float encoding and the blend state
-- and every one of those fails silently into zeros, which is indistinguishable from an effect
that was never switched on.

TWO FAULTS THE READBACK CAUGHT, both on its first use and both of them mine:
  - it latched on frame 1, the front-end menu, where there is no stage and no distance, and
    reported an entirely zero buffer. True, useless, and it would have read as "the G-buffer does
    not work". It now retries until a frame carries something, bounded, and reports the bound.
  - the route's own fixture put a solid at parallax depth 0.5, which shifts at TWICE the camera's
    rate and had walked off the side of the screen. The buffer honestly reported no surface
    normals; the geometry was drawn, correctly, where nobody could see it. The fixture now uses
    0.99 -- a hair in front of the fighters' plane, so it tracks the fight.

STILL TO DO, and this entry stays open for it: the defocus itself. The acceptance test this entry
already names is now runnable -- the fighters' plane in focus and a layer at a known depth not,
at every camera position -- because the distances are in the buffer and independently checkable.
