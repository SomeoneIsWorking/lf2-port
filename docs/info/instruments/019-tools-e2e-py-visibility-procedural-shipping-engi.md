---
id: I019
kind: instrument
status: trusted
created: 2026-08-22
---

## Instrument

tools/e2e.py visibility — procedural shipping-engine GPU visibility readback

## Validated by

On 2026-08-22 the same procedural scene showed four deliberately different outputs: unlit
occluder/character `#2040c0/#804020`, normal character mask `#000000/#ff0000`, reversed-order
mask `#ff0000/#ff0000`, and final lit occluder/character `#2040c0/#1e0f08`. The instrument
therefore demonstrates covered, visible, painter-reversed, and materially relit answers rather
than returning one uniform or asset-dependent result.

The shadow extension independently demonstrated its other answers on the shipping GPU path:
carried-object casting was white/white/black at fighter-only, held-object-only and clear samples;
the identical fighter-only mutation was white/black/black. A rear shadow under an
opaque/transparent foreground was black/white/white at blocked, transparent and unobstructed
samples, while reversing painter order made all three white. These are distinct caster and
receiver-depth failures, not one uniform mask.

The strict-depth extension added an earlier opaque ground receiver and an equal-depth caster
sample to those same occlusion arms. It produced black/white/white/black for later opaque,
transparent, earlier opaque and equal caster. Reversing painter order produced
white/white/white/black. A named `shadow-self-lequal` mutation changes only strict `LESS` to
`LESS_OR_EQUAL` and produced black/white/white/white, proving the equal-depth sample can show
the other answer.

## Known failure modes

(none recorded yet)
