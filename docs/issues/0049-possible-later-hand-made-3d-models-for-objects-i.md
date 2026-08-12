---
id: 49
title: Possible later: hand-made 3D models for objects, items and backgrounds (HD2D look)
status: open
symptom: not a defect -- a direction the reporter may ask for: some real 3D geometry among the sprites, hand-weaved and deliberately simple, to push the HD2D/remastered look further
tags: reported,rendering,renderer,hd2d,feature,future
created: 2026-08-11
updated: 2026-08-12
---

RAISED 2026-08-11 as something that MIGHT be asked for later, not as work to start now. The
reporter's words: "maybe I might ask for some 3D objects/items/backgrounds later on, to give
more of a HD2D/remastered look" and "hand-weaved models probably but nothing too complex".

Filed so that decisions taken between now and then do not quietly foreclose it -- that is the
whole reason this entry exists. It is NOT a request to build anything yet.

WHY THIS IS CLOSER THAN IT LOOKS. HD2D means 2D sprite actors in a 3D-lit world, and issue #30
has already put the port most of the way there: every draw is a QUAD in a display list
(runtime/video/render.c), the colour key becomes real alpha on upload, there is a fragment-shader
lighting pass with a key light expressed as a DIRECTION IN THE STAGE'S OWN AXES -- x across,
y up (LF2's jump axis, claim C018), z toward the camera -- hemisphere ambient, and sprite-cast
shadows sheared along that same light vector. A real model dropped into that world would be
lit by a light that already exists and already agrees with the sprites.

WHAT WOULD ACTUALLY HAVE TO CHANGE, so the size of it is known before it is asked for:
  - A DEPTH BUFFER AND A PROJECTION. The renderer draws quads in screen space with no depth
    test; the game's depth is a SORT (fn_0041a5a0 sorts objects before drawing) and the port
    inherits that ordering. Geometry that interpenetrates sprites needs a z the sort cannot
    express. LF2's z per object is real and is in the data (bg.dat's zboundary bounds it per
    stage, claim C021), so the axis exists -- what is missing is a camera matrix that maps the
    game's (x, y, z) into the same picture the sprites land in. That mapping IS the parallax
    the game already draws with, which is the useful clue: the stage's parallax rate is a
    projection, and issue #30 records that it already IS the projection rather than an
    approximation of one.
  - A MODEL FORMAT AND A LOADER, neither of which the port has. Hand-made and simple argues
    for glTF or OBJ, and for keeping the models in the repo with their own licence -- the repo
    ships no game content and a model is not game content, but it must be the project's own or
    redistributable.
  - WHERE A MODEL REPLACES WHAT. Replacing a stage layer, an item sprite or an object is a
    different job in each case, and the honest one to try first is a stage prop: the background
    layers are fully understood (claim C017, runtime/overrides/background.c gives each layer's
    picture, span, parallax rate, repeat step and y), so a prop can be placed against data the
    port already reads rather than against a guess.

THE STANDING CONSTRAINT APPLIES AND IS WORTH REPEATING HERE, because this is the kind of
feature that invites it: whatever is added must not fake what the game expresses. Issue #30
already records five effects -- bloom, depth of field, haze, vignette, colour grade -- that
shipped briefly and were cut because each touched every pixel and together they read as a
filter over a screenshot rather than as a remaster. A model that ignores the stage's own
geometry would be the same mistake in three dimensions.

DEPENDS ON, and these should land first: issue #48 (cast shadows fall on objects they should
not -- shadows have no depth today, and that is exactly the gap 3D geometry would widen),
issue #23 (a stage's sky has no picture beside it in a wide view), and issue #40 (GPU runs on
this machine are limited until Vulkan validation has run).

### Note (2026-08-12)
FEASIBILITY READ AGAINST THE RENDERER AS IT ACTUALLY STANDS -- not a plan, and nothing built.
This is the one thing that can be established without asking for the feature: whether the port
could take hand-made 3D models at all, and what the first real obstacle is.

WHAT THE NATIVE RENDERER IS TODAY: SDL's `gpu` backend driven through SDL_RenderGeometry (two
call sites in runtime/video/render.c), with committed SPIR-V fragment shaders for the isometric
light and the shadow mask, drawing per-quad into a full-resolution render target.

THE OBSTACLE, AND IT IS STRUCTURAL RATHER THAN A MATTER OF EFFORT: there is NO DEPTH BUFFER.
The single "depth" match in render.c is the display list's depth SORT -- the painter's ordering
the game itself computes in fn_0041a5a0 -- and hd2d.c has none at all. SDL_RenderGeometry is a
2D triangle submission path; it has no depth attachment and no depth test to enable.

WHAT FOLLOWS. Flat sprites can be painter-sorted because LF2 sorts them for us and they never
interpenetrate. A hand-made MESH does interpenetrate -- with itself, first of all -- so it
cannot be drawn correctly by submitting triangles in sorted order. So this entry is not "model
some objects and draw them"; its first step is a depth attachment, which means either SDL_GPU
directly rather than SDL_Render, or a depth pre-pass faked into the existing shader path. That
is a renderer change of the same size as issue #30's original work, and it belongs to #30
rather than here.

WHY RECORD IT: the entry reads as a small additive nicety ("maybe some 3D objects later"), and
it is not. Anyone picking it up should know the first commit is a depth buffer, before any
modelling is done -- otherwise the models arrive and cannot be drawn.

NOTHING HAS BEEN BUILT, and nothing should be until the reporter asks. This is a cost estimate,
recorded so the cost is known when the question is asked rather than discovered afterwards.
