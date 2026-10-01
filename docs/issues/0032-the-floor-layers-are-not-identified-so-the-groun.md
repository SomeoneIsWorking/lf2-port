---
id: 32
title: The floor layers are not identified, so the ground cannot be lit as a ground plane
status: closed
symptom: the lighting treats every surface as a camera-facing billboard. The floor a fighter stands on is a HORIZONTAL surface and would catch the key light differently -- that is real 3D shading from geometry that genuinely exists, and it is the one honest piece of 'make the stages 3D'. It needs the walkable z band (bg.dat zboundary) or the floor layers themselves located in the background record, and neither is
tags: reported,rendering,re,background
created: 2026-08-06
updated: 2026-09-09
---

### Note (2026-08-06)
MEASURED, not just intended. Over a match frame's floor with the fighters excluded (14137
pixels sampled): mean luminance change -0.03 levels of 255, mean chroma change 3.0 levels.
So the ground does pick up the sky's colour and the picture does not get brighter or darker.
Over the backdrop above the floor line, 0 of 16880 sampled pixels changed at all.
The one thing not to over-claim: the tint is a PER-CHANNEL multiplier, so it is the tint that
carries unit luminance, not every pixel. A strongly saturated albedo can still move -- the
largest single |dluminance| seen on the floor was 56 levels. The mean is what the luminance
lock guarantees.
