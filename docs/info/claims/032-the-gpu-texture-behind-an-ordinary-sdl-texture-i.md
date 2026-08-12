---
id: C032
kind: claim
status: falsified
created: 2026-08-12
tags: renderer,hd2d
depends: runtime/video/mesh.c
falsified_on: 2026-08-12
---

## Claim

The GPU texture behind an ORDINARY SDL_Texture is readable through SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER on the gpu renderer -- so the mesh pass can sample the textures the display list has already uploaded rather than keeping a second copy of every stage bitmap on the GPU

## Evidence

Standalone spike ($CLAUDE_JOB_DIR/tmp/rev.c) under gpuguard, 2026-08-12, both classes: on the 'gpu' renderer a texture made with plain SDL_CreateTexture(STATIC) and filled with SDL_UpdateTexture reports its GPU handle as READABLE; on the 'software' renderer the property is absent, which is the negative showing the query discriminates. This is the REVERSE of claim C030, which covers a texture wrapped AROUND a GPU texture the caller made. VRAM delta 0, no amdgpu trouble lines.

## What would falsify it

a handle that reads non-null but is not the texture's current storage -- SDL may reallocate on SDL_UpdateTexture or on a format change, so a handle cached across frames could go stale; the spike read it once and did not re-read after a second update

## FALSIFIED 2026-08-12

FALSIFIED for the purpose it was recorded for. The handle IS readable -- that part stands -- but a texture sampled through it from the mesh pass's OWN command buffer comes back all zeros, rgba(0,0,0,0), which is exactly the falsifier this claim named. Discriminated against three controls in one run rather than guessed: (1) the same quad with no texture reads rgba(255,255,255,255), so the geometry rasterises; (2) a texture the pass UPLOADS ITSELF, through its own transfer buffer, sampled by the same pipeline and the same UVs, reads rgba(255,0,0,255) on the left and rgba(0,255,0,255) on the right -- so the sampler, the UVs and the pipeline are all correct; (3) SDL_FlushRenderer before the read, and an SDL_RenderTexture draw of the source first, change nothing, so it is not a lazy upload waiting on a flush. What is left is that SDL's own texture is not in a state a foreign command buffer can sample. CONSEQUENCE: the mesh pass must own its uploads, so the stage's art is on the GPU twice.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
