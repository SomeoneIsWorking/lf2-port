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

At 1920x1080 the match first appeared at frame 1212. RmlUi opened at `@match+300` and mapped
Cancel closed it at `@match+360`. The first frame with the document hidden was frame 1574. In
the failing build it had black gaps, missing HUD sections, and garbled text tiles; its non-black
coverage was 0.824556 against 0.885122 before opening, only 93.2% of the completed frame.

The cause was in `present_primary`: HUD and device decorations recorded tiles before
`render_hold_begin` tried to reacquire the old frame. The prior present had marked the list
spent, so that recording call cleared the display lists, tile arena, and pooled-texture claims.
The hold then restored only lengths and high-water metadata over backing that had already been
reused. Transparent RmlUi exposed the corrupted base while open; closing it during
`rmlui_render` exposed one naked corrupted frame. The software path separately repeated the
same alpha-composited SVG over unchanged primary pixels.

Adding a validity bit proved the deeper design defect rather than fixing it: drawing can begin
after LF2's mid-update present and before the pause override next runs, so mutable list backing
can legitimately change before a hold is acquired. A list-length rewind cannot own a frozen
frame.

## Resolution

Frozen presentation now owns an immutable completed frame:

- Live native presentation draws the game and port decorations once, copies the finished
  output-resolution target to `render_snapshot.c`, then composites RmlUi.
- Frozen native presentation restores only a successfully copied snapshot for the same
  composition source, composites RmlUi, and never replays the old display list.
- Live/frozen mode is latched before RmlUi callbacks, so a mapped close during rendering cannot
  change which frame source that present owns.
- A paused resize aspect-preserves the captured raster. The software backend retains the last
  completed primary region and skips repeated decorations, so widened surface metadata cannot
  expose unwritten columns or accumulate SVG alpha.
- The old hold lengths, tile-arena rewind, pooled busy-state rewind, and pause painter call
  boundaries were deleted. Deferred display-list clearing remains because LF2's actual present
  is inside its update and next-frame drawing can begin after it; that is a producer boundary,
  not pause retention.

`tests/test_framelife.c` checks snapshot validity, composition identity, failed-copy
invalidation, frozen counting, resize containment, the true deferred-clear boundary, overlay
separation, and tile-pool reuse offline. `tools/e2e.py ui_global` captures six exact GPU frames
around open/close at 1920x1080; the first closed frame now has 0.8851 coverage against 0.8851
before opening. It also proves the modal appeared and was removed rather than accepting an
unchanged buffer. `tools/e2e.py pause_dropout` measured 121 native snapshot presents in a real
paused match.

### Resolution (2026-08-22)
Replaced mutable display-list rewind with an output-resolution immutable completed-frame snapshot captured before RmlUi; latched transition ownership, preserved completed dimensions across paused resize, and added exact 1920x1080 open/close pixel verification.
