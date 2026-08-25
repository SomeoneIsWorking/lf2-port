---
id: 111
title: The key light has no intensity setting; only its angle is adjustable
status: resolved
symptom: The GRAPHICS tab sets the light's azimuth and elevation, but the key intensity is the constant 1.48 behind LF2_HD2D_KEY -- a player cannot make the shading stronger or softer without a diagnostic env var.
tags: reported,feature,options,hd2d,rmlui
created: 2026-08-25
updated: 2026-08-25
---

## Reported

USER 2026-08-25: "Add light intensity setting in RmlUi".

## The shape

options.c holds it (float, default the shipped 1.48), hd2d_light_uniforms reads it for
u_sun_dir.w, and the RmlUi GRAPHICS tab gets a slider beside the two angle rows. Persisted
through config.c as light_intensity. The LF2_HD2D_KEY env knob stays honoured as a test-arm
pin exactly like LF2_HD2D (issue #69 pattern): unset, the option rules.

### Resolution (2026-08-25)
The key light's strength is an options.c value (default the shipped 1.48), a percentage slider on the RmlUi GRAPHICS tab beside the two angle rows, and the config key light_intensity; hd2d_light_uniforms reads it for u_sun_dir.w and LF2_HD2D_KEY stays a first-read route pin. Verified in a 1280x900 offscreen run: the GRAPHICS page shows the Intensity row and the slider value reaches the light through hd2d_light_set_intensity.
