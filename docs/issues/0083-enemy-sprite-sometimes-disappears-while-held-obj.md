---
id: 83
title: Enemy sprite sometimes disappears while held object remains visible
status: closed
symptom: During gameplay an archer can become completely invisible while the rock it is holding still renders at the correct position
tags: reported,rendering,sprite,hd2d,enemy
created: 2026-08-21
updated: 2026-09-09
---

## Reported
USER 2026-08-21: "enemies sometimes go invisible, there is an archer here holding this rock".
The supplied screenshot shows the rock at the expected held position while the archer sprite is
absent. Preserve that distinction while tracing: the object exists and its attached object's draw
survives, so this is not evidence that the gameplay object was deleted.
## Root cause
## What was tried / dead ends
