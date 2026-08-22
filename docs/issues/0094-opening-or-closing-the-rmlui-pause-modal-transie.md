---
id: 94
title: Opening or closing the RmlUi pause modal transiently glitches the frame
status: resolved
symptom: Opening and closing RmlUi causes visible glitches during the transition; the corruption does not persist into gameplay.
tags: reported,ui,rmlui,pause,rendering
created: 2026-08-22
updated: 2026-08-22
---

## Reported

The user reports that opening and closing RmlUi causes visible glitches. The glitches are transient: they do not persist in the game after the modal transition.

## Constraint

Fix the frame-lifetime or composition transition that produces the transient corruption. Do not hide it with an extra redraw, delay, suppression, or screen-specific special case. The retained game frame, native display list, and RmlUi composition must have one explicit ownership/lifetime rule across both opening and closing.

## Investigation

The first implementation froze matches by declining `fn_004246b0__orig` and invented a second
presentation lifecycle for the missing game frame. Retaining display-list lengths was invalid:
present-time decorations could clear the spent list and reuse its tile arena before the hold was
reacquired, so restoring metadata resurrected entries over changed backing. Replacing that with
an immutable output texture removed that corruption but retained the deeper architectural error:
RmlUi still switched the game between live and snapshot render paths.

Closing can also happen *inside* `rmlui_render`: mapped Cancel is dispatched by
`rmlui_input_update`, whose callback hides the document and clears its active flag. The old
function nevertheless continued through `Context::Update`, `Context::Render`, and the backend
frame after the close. The frozen/live state had already been latched by `present_primary`, so
that present also remained on the snapshot side of the transition. This is why a later captured
frame could look recovered while the first hidden frame still glitched.

## Resolution

There is now one frame lifecycle: the game's ordinary one.

- `fn_004246b0__orig` always runs, so every screen—including a match beneath RmlUi—continues
  through its normal update, display-list build, render, and present. Modal behavior is owned by
  the existing input boundary, which consumes physical input before LF2 sees it.
- Native snapshot capture/restore, software completed-region retention, frozen presentation,
  and the on-demand pause present were deleted. RmlUi composites after the current native or
  software game frame exactly as it does on non-match screens.
- `rmlui_lifecycle.h` gives each open document generation a frame token. If an input or update
  callback closes or replaces the document, that token is invalid and the interrupted RmlUi
  frame stops before backend rendering. This is lifecycle cancellation, not suppression or an
  extra redraw.
- Presentation immediately clears display-list lengths, tile-arena usage, and pooled-texture
  claims. LF2's real present occurs inside its update, so any later record simply begins the
  next ordinary frame in an already-empty list; there is no spent/retained state.

`ctest rmlui_lifecycle` exercises ordinary completion, close-during-frame, and close/reopen
replacement against the shipping header. `tools/e2e.py ui_global` runs a deterministic no-match-
modal control at the same `@match` offsets: the two open-modal captures must match that control
exactly outside the centered document, and the first hidden frame at +362 plus its two successors
must match the full control frames byte-for-byte. The +360 action reaches RmlUi after the
+360/+361 dumps, so those remain modal frames and are not close evidence. Coverage alone cannot
accept stale replay.

### Resolution (2026-08-22)
Removed the second frozen-frame lifecycle rather than retaining another picture of the game: fn_004246b0__orig now always owns the ordinary update/draw/present beneath modal RmlUi, and input ownership alone makes the shell modal. Closing from rmlui_input_update had hidden the document but continued Context::Update/Render in that same UI frame; a document-generation token now cancels the interrupted frame before backend rendering. Deleted native snapshot capture/restore, software completed-region retention, and on-demand frozen present. Added offline lifecycle checks and an exact ui_global route that targets the first hidden frame after close and requires live pixels beneath the modal.

### Note (2026-08-22)
Follow-up audit removed the last spent/fl_touch deferred-clear state: present now clears list lengths, overlay boundaries, tile-arena use, and pool claims immediately. ui_global no longer accepts coverage as proof; it runs a deterministic no-match-modal control at the same @match offsets, requires exact outside-document pixels while open, and byte-identical full frames for the first hidden frame (+362) and two successors.
