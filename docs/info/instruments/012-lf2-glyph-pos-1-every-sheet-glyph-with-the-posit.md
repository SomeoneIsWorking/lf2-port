---
id: I012
kind: instrument
status: trusted
created: 2026-08-11
---

## Instrument

LF2_GLYPH_POS=1 -- every sheet glyph with the position the game asked for, plus cam= and draw= on the same line

## Validated by

VALIDATED on the y-398 player tag in stage mode: it prints the tag's x (609..785), its call site (0041ab26, inside FUN_0041a5a0), and the camera at that instant.

THE cam=/draw= COLUMNS ARE THE POINT, and they were added after the probe's first two readings were BOTH misread. 'x is identical at 794 and at 1920' was taken twice as proof the tag misses the widescreen camera shift; the camera was 0 in every one of those runs, and below bg_draw_camera's zero clamp the shifted and unshifted hypotheses predict the SAME x. The probe was working -- the COMPARISON was vacuous, and nothing in the output said so.

HOW TO USE IT WITHOUT REPEATING THAT: a difference (or absence of one) between two view widths means nothing on a frame where cam=0. Read draw= != cam before drawing any conclusion about a widescreen offset; if the run never gets there, the run is not evidence and the pad script needs to walk further, not the conclusion to be written up.

A run whose fighter never moves cannot reach the clamp at all: the default stage-mode route ends at the overlay confirm and sits still, so it is unusable for this question by construction.

## Known failure modes

(none recorded yet)
