---
id: 75
title: HD2D must only shade characters and cast shadows; remove DoF and global effects
status: resolved
symptom: The shipped HD2D path applies depth of field and other whole-frame treatment, but the requested presentation is character shading plus cast shadows only; backgrounds and all other pixels must remain the authored art.
tags: reported,renderer,hd2d,dof,shadows
created: 2026-08-20
updated: 2026-08-20
---

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-08-20)
The shader chain was reduced to a character mask, projected cast-shadow mask and one light pass. Backgrounds, stage geometry, HUD and text retain authored colour; all DoF, G-buffer distance, bloom, floor/backdrop treatment and related options/instruments were removed. The render route's menu negative changes zero pixels.
