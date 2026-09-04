---
id: C029
kind: claim
status: holds
created: 2026-08-12
tags: renderer,hd2d
depends: runtime/video/render.c, runtime/win32/win32.c
reconfirmed: 2026-08-21
verified_at: 2026-08-21 11:37:26
---

## Claim

SDL_GetGPURendererDevice returns a usable SDL_GPUDevice for a renderer created as SDL_CreateRenderer(window, "gpu"), and that device supports a D32_FLOAT depth-stencil target -- so the port can add a depth-tested offscreen pass on its EXISTING device without moving off SDL_Render

## Evidence

Standalone spike ($CLAUDE_JOB_DIR/tmp/gpuspike.c) run under gpuguard on 2026-08-12, both classes: the 'gpu' renderer gives device=YES, driver=vulkan, D32_FLOAT SUPPORTED as a depth-stencil target, and a 256x256 depth texture allocates OK; the 'software' renderer gives device=no, which is the negative that shows the query discriminates rather than returning non-null for everything. No amdgpu trouble lines, VRAM delta 0 MiB. SDL_render.h itself contains the string 'depth' zero times, which is why the 2D path cannot do this alone.

## What would falsify it

a machine whose gpu backend reports device=no (an SDL build without the GPU renderer, or a driver with no Vulkan/D3D12/Metal), or a depth format query that succeeds while the pipeline creation using it fails -- the spike allocates a texture but does not yet build a graphics pipeline

## Re-confirmed 2026-08-21

After the renderer changes, `./run.sh` brought the Vulkan engine up on SDL_GetGPURendererDevice and the recorded renderer comparison completed every GPU/engine arm headlessly.
