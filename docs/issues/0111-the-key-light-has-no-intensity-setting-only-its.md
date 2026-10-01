---
id: 111
title: The key light has no intensity setting; only its angle is adjustable
status: closed
symptom: The GRAPHICS tab sets the light's azimuth and elevation, but the key intensity is the constant 1.48 behind LF2_HD2D_KEY -- a player cannot make the shading stronger or softer without a diagnostic env var.
tags: reported,feature,options,hd2d,rmlui
created: 2026-08-25
updated: 2026-09-09
---

## Reported
USER 2026-08-25: "Add light intensity setting in RmlUi".
## The shape
options.c holds it (float, default the shipped 1.48), hd2d_light_uniforms reads it for
u_sun_dir.w, and the RmlUi GRAPHICS tab gets a slider beside the two angle rows. Persisted
through config.c as light_intensity. The LF2_HD2D_KEY env knob stays honoured as a test-arm
pin exactly like LF2_HD2D (issue #69 pattern): unset, the option rules.
