---
id: 63
title: Bloom and depth of field over the pixel art, grounded in real depth rather than in a screen filter
status: resolved
symptom: Two thirds of issue #30's HD2D ask are delivered and one is not: the reporter asked for 'depth, lighting, bloom/DOF over the pixel art' and there is no bloom and no depth of field. A previous cut of both was removed because each touched every pixel and together they read as a filter over a screenshot.
tags: reported,renderer,hd2d,feature
created: 2026-08-12
updated: 2026-08-20
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
- Nothing that changes a frame with no fighters and no stage geometry in it. `tools/e2e.py
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

### Note (2026-08-12)
### Note (2026-08-12) -- the DEPTH OF FIELD is built, and it passes this entry's own test

runtime/shaders/dof.frag, presented through engine_present. On by default; `LF2_DOF=off` is the
A/B control arm, the same shape LF2_HD2D=off has for the lighting.

THE TEST THIS ENTRY ASKED FOR, run:

    frame_000401 (character selection, NO stage)   the defocus changes NOTHING -- max diff 0
    frame_001351 (a match)                          it changes 84395 px by up to 31

Those are opposite on the two frames on purpose, and the first is the one that matters: it is
what catches an effect spreading over the whole picture, which is why the previous
bloom/DOF/haze/vignette cut was deleted. And it holds BY CONSTRUCTION rather than by tuning -- a
pixel with no distance in the G-buffer takes an untouched branch, and a menu frame has no layers,
so every pixel there takes it.

THE FIGHTERS ARE NEVER BLURRED, and not as a special case: they write no distance at all, so they
take the same untouched branch the HUD does. The focal plane is 1.0 by DERIVATION rather than by
taste -- a parallax depth of 1 is the plane an object shifts with the camera at rate 1, which is
where the game puts every fighter (C018/C031).

THE MEASURE IS 1/d, NOT d. Defocus is a difference of reciprocals, and the reciprocal is what
makes it bounded: the shipped stages run from about 0.89 to 535, and 1/d maps all of that into
(0, 1.1] with the fighters' plane at exactly 1. Using d directly would put almost the whole range
in the last few percent of the blur and make every stage look the same.

A TAP ONLY COUNTS IF IT IS AT THE SAME DISTANCE -- a ratio, not a difference, because distance is
a scale (1.0 against 1.2 is a real step between layers; 500 against 535 is the same sky). Without
it a blurred sky pulls the colour of a sharp fighter out across its own silhouette, which is the
classic halo and exactly what would make this read as a filter again.

The radius is in texels of the OUTPUT (oh/110, i.e. 5 texels at the game's own 550 rows), so it
scales with the window. A fixed pixel radius would be a heavy blur at 794x550 and a hairline at
4K -- a blur that is a function of resolution rather than of distance.

KNOWN AND BOUNDED, recorded rather than left to be discovered: the defocus is applied where the
engine's frame is copied out, which is BEFORE hd2d's lighting rather than after it. Physically a
lens defocuses everything, so cast shadows and the floor tint stay sharp on a surface that has
been blurred. It is invisible on the stages measured so far because both things it touches sit at
depth 1.0 -- the floor a shadow falls on is in focus by definition. It will show on a stage whose
shadow-receiving floor is a distant layer, and the fix is to move the defocus after hd2d_post,
which needs the lit frame as the shader's source rather than the engine's own target.

STILL OPEN: the BLOOM half of this entry.

### Note (2026-08-12)
## The DOF is in and verified. THE LUMINANCE BLOOM IS REJECTED, by measurement.

The depth of field ships: `runtime/shaders/dof.frag`, driven by the G-buffer's distance
channel, focus at the fighters' plane (1.0), asserted opposite ways on two frames by
The recorded renderer comparison changed 0 pixels on a frame with no stage in it and 84,395 pixels in a match.

A bloom was written to the same shape (threshold + world gate) and **it glowed nothing at all**.
The cause is not a bug in it, and this is the part worth keeping:

**What was measured, frame side** (`LF2_ENGINE_GBUF=1`, extended to read the colour target back
BESIDE the G-buffer so the conjunction can be counted rather than inferred):

    over 0.75 luminance: 766 px, of which 0 carry a distance and 766 do not
    world luminance >= 0.50 : 1581 px (0.501% of the world)   <- and nothing above it

Every bright pixel in a match frame is HUD, text or a sprite. The threshold and the gate were
each measured alone and each looked fine; their INTERSECTION was empty. That is why the
instrument now reports the conjunction -- two histograms that agree with themselves can hide
this indefinitely.

**What was measured, art side** (`tools/re/stage_lum.py`, all 133 shipped layers, offline, black
key excluded):

    bc  (Brokeback Clif)   0.000% >= 0.75,  max luminance 0.541
    gw  (Great Wall)      22.602% >= 0.75,  max luminance 1.000

Both extremes are in the shipped art. No absolute threshold behaves as a bloom on both: at or
above 0.55 it selects literally nothing on bc, and at 0.75 it glows a quarter of gw. **A
relative threshold does not rescue it either** -- gw's top-1% percentile is 1.000, because more
than one percent of its art is exactly white (its sky is near-white art, verified: no shipped
layer keys on white; gw/sky.bmp is 3.91% pure white and it is a sky, not a cut-out).

## The conclusion, and it is a decision rather than an open question

**A luminance-thresholded bloom does not ship.** Luminance is not a measured quantity here, it
is a chosen one, and the choice behaves oppositely on two shipped stages. That is exactly the
defect issue #30 cut the first bloom for. The DOF cleared the bar because distance comes from
bg.dat; this does not clear it, and shipping it with a number tuned until one stage looked right
would be the bandaid.

The code was removed rather than left switched off (it was never committed): `bloom.frag`, the
bloom target and render state in `engine.c`, `engine_bloom_enabled`, `LF2_BLOOM`, and the route
arm. What was KEPT is the evidence -- the joint readback and `stage_lum.py`.

## What a bloom would have to be instead

An EMITTER, authored. The stage art contains real light sources (`cuhk/lamp1.bmp`,
`lamp2.bmp`), and "this surface emits" is a statement the artist makes, checkable, and per
stage -- the same standing distance has. That belongs in the port's own `.stage` files beside
the geometry (issue #62), not in a screen-space threshold. Shape: an emissive flag on a solid,
a second draw of only the emissive geometry into the half-res target, blurred and added. No
threshold, no gate, nothing to tune. Until a stage authors an emitter there is no glow, and
that is correct rather than a missing feature.

Blocked on #62 having any authored content at all -- `stages/` currently holds a README.

### Resolution (2026-08-20)
The later presentation requirement superseded bloom/DoF: the DoF pass, G-buffer distance, bloom-era diagnostics and their options were deleted. The engine now applies only character shading and cast shadows; the render route proves a menu frame changes by zero pixels while a fighter frame changes.
