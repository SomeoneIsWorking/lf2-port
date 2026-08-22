---
id: 97
title: Cast shadow does not meet the fighter's feet
status: resolved
symptom: The native cast shadow is visibly detached and laterally misaligned from fighters' feet across multiple characters
tags: reported,rendering,shadows,hd2d,characters
created: 2026-08-22
updated: 2026-08-22
---

## Reported

USER 2026-08-22: "the feet and shadow don't align properly"

The supplied screenshots show the cast silhouette beginning away from the visible foot contact area and extending with an incorrect lateral/forward anchor on multiple character shapes. Identify the character/ground geometry and projection error. Fix the shared shadow anchor/projection formula; do not add per-character offsets, screen-space nudges, or asset-specific cases. Verify at least two different fighter silhouettes and include a negative that exposes an incorrect anchor.

USER 2026-08-22: "shadow feet fix should be hand-authored as well"

## Root cause and implementation

The frame contact offsets were already hand-authored. Each LF2 frame supplies `centerx` and
`centery`, and `fn_0040de30` consumes them when it places that frame's destination rectangle.
The native shadow path discarded the authored horizontal result by rebuilding the projected
quad as `ellipse_center +/- frame_width/2`. It also treated the ellipse's bottom edge as height
zero, though the object z row is the ellipse centre and the actual floor/contact row.

`stagelight_shadow_quad` now projects each corner from the authored destination rectangle:
for a point `(px,py)`, its height is `object_z-py` and its ground projection is
`(px+height*across, object_z-height*up)`. No character name, sprite ID, per-frame nudge, or
alpha-derived centre exists in the renderer. The production-header unit gate uses an
off-centre 40x80 frame at x=86: the accepted foot edge remains 86..126, while the former
ellipse recentering would produce 80..120 and fails the relation.

### Implementation status (2026-08-22)
The renderer now projects every source point from the frame rectangle already placed by LF2's
authored centerx/centery, using the object's exact z row as height zero; it no longer recentres
the frame on the ellipse or anchors to its bottom edge. Two production-header geometry
relations and the off-centre old-formula negative pass. Those procedural rectangles are not
evidence that two actual LF2 fighter silhouettes meet their shadows, so acceptance remains
open below.

### Reopened (2026-08-22)
Implementation and synthetic production-path relations are complete, but acceptance remains open: capture and measure opaque foot contact against projected shadows for at least two distinct real LF2 fighter silhouettes, with the former recentering as the negative.

`tools/e2e.py shadow_contact` is the queued real-data acceptance. It captures paired shipping
character and shadow masks at nine deterministic match offsets, selects the same frame's two
distinct fighter-sized opaque silhouettes once both are grounded, and measures the nearest
shadow pixel to each opaque foot band. An initial capture at `@match+120` correctly refused the
airborne second fighter (1 px grounded contact vs 9 px airborne separation), so airborne
projection cannot be mistaken for detachment. Its synthetic other-answer rejects a seven-pixel
separation; issue #72's real old-path trace measured the former recentering at 7 px and 12 px.
The corrected multi-frame route passed. At `frame_001369`, the two grounded silhouettes were
bbox `(241,270)..(278,331)`, 1515 opaque pixels, 0 px contact gap and bbox
`(313,267)..(350,333)`, 1903 opaque pixels, 1 px gap. The earlier captured airborne fighter
remained 9 px above its projected shadow in two frames, so the acceptance does not force an
airborne silhouette onto its screen-space feet.

### Resolution (2026-08-22)
The shared projection preserves LF2-authored frame placement and object z. Real GPU/game masks at deterministic frame_001369 verified two distinct grounded fighters: bbox (241,270)-(278,331), 1515 opaque pixels, 0px foot/shadow gap; bbox (313,267)-(350,333), 1903 opaque pixels, 1px gap. Airborne frames remained separated at 9px as required, while the analyzer rejects the old traced 7px/12px recentering.
