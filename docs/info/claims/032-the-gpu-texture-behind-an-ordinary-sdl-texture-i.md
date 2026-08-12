---
id: C032
kind: claim
status: holds
created: 2026-08-12
tags: renderer,hd2d
depends: runtime/video/mesh.c
---

## Claim

The GPU texture behind an ORDINARY SDL_Texture is readable through SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER on the gpu renderer -- so the mesh pass can sample the textures the display list has already uploaded rather than keeping a second copy of every stage bitmap on the GPU

## Evidence

Standalone spike ($CLAUDE_JOB_DIR/tmp/rev.c) under gpuguard, 2026-08-12, both classes: on the 'gpu' renderer a texture made with plain SDL_CreateTexture(STATIC) and filled with SDL_UpdateTexture reports its GPU handle as READABLE; on the 'software' renderer the property is absent, which is the negative showing the query discriminates. This is the REVERSE of claim C030, which covers a texture wrapped AROUND a GPU texture the caller made. VRAM delta 0, no amdgpu trouble lines.

## What would falsify it

a handle that reads non-null but is not the texture's current storage -- SDL may reallocate on SDL_UpdateTexture or on a format change, so a handle cached across frames could go stale; the spike read it once and did not re-read after a second update
