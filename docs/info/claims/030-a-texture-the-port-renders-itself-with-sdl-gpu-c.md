---
id: C030
kind: claim
status: holds
created: 2026-08-12
tags: renderer,hd2d
depends: runtime/video/engine.c, runtime/video/render.c
reconfirmed: 2026-08-21
verified_at: 2026-08-21 11:37:26
---

## Claim

A texture the port renders itself with SDL_GPU can be handed to SDL_Render as an ordinary SDL_Texture with NO copy: SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER wraps the same object, and SDL_RenderTexture draws it

## Evidence

Standalone spike ($CLAUDE_JOB_DIR/tmp/bridge.c) under gpuguard, 2026-08-12, both classes. Positive, 'gpu' renderer: a 256x128 R8G8B8A8 texture created with COLOR_TARGET|SAMPLER usage wraps OK, and SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER read back off the wrapper returns the IDENTICAL pointer (same_object=YES) -- which is what rules out SDL ignoring the property and making a blank texture -- and SDL_RenderTexture draws it. Negative, 'software' renderer: no device at all, so there is nothing to wrap and the arm says so. VRAM delta 0 MiB, no amdgpu trouble lines.

## What would falsify it

a wrap that reports OK while SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER reads back a different pointer, or a backend where the wrapped texture draws blank -- the spike drew it but did not read the pixels back, so 'draw=OK' is the API accepting it and not yet a picture

## Re-confirmed 2026-08-21

After the renderer changes, tools/e2e.py render read back and compared both engine frames through the wrapped SDL_Texture path; the engine matched the software frame within the established tolerance on menu and match captures.
