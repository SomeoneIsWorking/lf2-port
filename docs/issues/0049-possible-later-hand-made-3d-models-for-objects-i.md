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

### Note (2026-08-12)
### Note (2026-08-12) -- the blocker is smaller than the estimate said: depth is ADDITIVE

The feasibility note above says the first commit is a depth buffer, and that this "means either
SDL_GPU directly rather than SDL_Render, or a depth pre-pass faked into the existing shader
path... a renderer change of the same size as issue #30's original work". Read against SDL 3.4's
own headers, the first half of that dichotomy is a false one.

WHAT THE HEADERS SAY (both checked, /usr/include/SDL3):
  - SDL_render.h contains the string "depth" ZERO times. There is no depth attachment and no
    depth test on the 2D path. That half of the estimate stands.
  - SDL_render.h also exposes `SDL_GetGPURendererDevice(SDL_Renderer *)` and the property
    `SDL_PROP_RENDERER_GPU_DEVICE_POINTER`. The `gpu` backend the port already asks for BY NAME
    (claim C020) is built on an SDL_GPUDevice the port can take a handle to.

SO THE SHAPE IS: one device, two consumers. Everything the port draws today keeps going through
SDL_Render exactly as it does. Hand-woven geometry is rendered by a SEPARATE SDL_GPU pass on
the SAME device, into its own colour target with its own depth attachment, depth-tested
properly against itself -- and the finished texture is then handed to the display list as an
ordinary quad. No rewrite of render.c, no second device, no change to the software fallback,
and the recorded background identity arm is untouched because a stage with no geometry
submits no pass.

WHY THAT IS ENOUGH FOR WHAT WAS ACTUALLY ASKED FOR, and this is the part the estimate did not
separate: issue #62 asks for the LEVELS to be woven in 3D -- the set the fighters stand in, not
the fighters. A set is BEHIND every sprite, so it never has to interpenetrate one; the
interpenetration that genuinely needs a shared depth buffer is mesh-against-mesh, and that is
entirely inside the offscreen pass. LF2's own painter order continues to place the sprites, as
it always has.

WHAT IS STILL UNVERIFIED and needs a spike on the GPU: that
`SDL_GetGPURendererDevice` returns a usable device for a renderer created as
`SDL_CreateRenderer(window, "gpu")` rather than via `SDL_CreateGPURenderer`. The property is
documented for the backend, but "documented" is not "measured" -- and if it returns NULL the
port would create the device itself and pass it to `SDL_CreateGPURenderer`, which is a
three-line change to window creation rather than a rewrite either way.

This does not make #62 small. It makes its FIRST commit small, which is what the estimate was
about.

### Note (2026-08-12)
### Note (2026-08-12) -- measured: the device, the depth format and the bridge all work

The note above reasoned from the headers that depth could be additive. It has now been RUN,
both classes, under gpuguard -- claims C029 and C030:

    gpu       renderer=gpu device=YES depth_d32=SUPPORTED driver=vulkan alloc=OK
    software  renderer=software device=no
    gpu       wrap=OK same_object=YES draw=OK
    software  no device, so nothing to wrap -- the negative fired

`same_object=YES` is the load-bearing one: reading SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER back
off the wrapper returns the identical pointer, so SDL is holding the port's own GPU texture
rather than having ignored the property and made a blank one.

So the estimate in the feasibility note -- "a renderer change of the same size as issue #30's
original work" -- does not apply to the first commit. The mesh pass shares the device the port
already has, renders into its own colour and D32_FLOAT depth targets, and the finished colour
target is wrapped as an SDL_Texture the existing display list draws. No copy, no rewrite of
render.c, no second device, and the software compositor is untouched.

This entry stays open for the MODELS. The renderer work moved to #62, which is the entry that
asked for a use for them.

### Note (2026-08-12)
### Note (2026-08-12) -- the BACKGROUNDS third of this entry has moved to #62 and is underway

This entry covers three things -- models for objects, for items and for backgrounds. The third
is now issue #62, which the reporter asked for directly, and it has taken all the renderer work
with it:

  - the depth-tested offscreen pass exists (runtime/video/mesh.c), sharing the renderer's own
    device, with a self-test a broken depth test fails;
  - the projection is built and agrees with the game's own layer placement on real stages;
  - both directions of the GPU-texture bridge are measured (C030, C032).

So this entry's stated blocker -- "the first commit is a depth buffer, before any modelling is
done" -- is discharged. What remains here is the OBJECTS and ITEMS thirds, and they are a
different problem from the backgrounds one in a way worth stating before anyone starts:

  - a background layer is drawn ONCE per frame from data the port fully reads, so replacing it
    with geometry is a swap at a known place;
  - an object is drawn by fn_0041a5a0 (hand-ported, runtime/overrides/objects.c) from a sprite
    sheet chosen by the character's animation frame. Replacing that with a model means the
    model has to have every frame's POSE, or a rig and an animation mapping. LF2 has 23
    playable fighters with hundreds of frames each. That is an art project, not a port task,
    and nothing about the renderer makes it smaller.

Recorded so the depth-buffer work is not read as having unblocked the objects half. It has not.
