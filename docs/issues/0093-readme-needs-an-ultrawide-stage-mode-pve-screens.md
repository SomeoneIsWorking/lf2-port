---
id: 93
title: README needs an ultrawide Stage Mode PvE screenshot
status: resolved
symptom: The README showcases 16:9 Demo combat but has no ultrawide capture of the port's Stage Mode PvE path.
tags: reported,docs,readme,screenshots,ultrawide,stage-mode,pve
created: 2026-08-22
updated: 2026-08-22
---

Add a fresh deterministic screenshot of actual Stage Mode PvE at an ultrawide aspect ratio. The capture must come from the native renderer at an ultrawide window size, not from cropping or stretching the existing 16:9 Demo image. Include it in the README showcase gallery and retain the narrow promotional-screenshot asset-policy exception.

### Resolution (2026-08-22)
Added a deterministic 3440x1440 Stage Mode PvE capture to the README. The run used LF2_MODE=stage with the native renderer, reached match state, reported the stage-mode section lock binding the ultrawide camera, and fired all 48 scripted inputs. The selected Stage 1-1 frame shows Firen and a Bandit separately around a crate without cropping or stretching. PNG dimensions and README link were checked, and all 26 offline tests pass.
