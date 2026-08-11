---
id: 49
title: Possible later: hand-made 3D models for objects, items and backgrounds (HD2D look)
status: open
symptom: not a defect -- a direction the reporter may ask for: some real 3D geometry among the sprites, hand-weaved and deliberately simple, to push the HD2D/remastered look further
tags: reported,rendering,renderer,hd2d,feature,future
created: 2026-08-11
updated: 2026-08-11
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
