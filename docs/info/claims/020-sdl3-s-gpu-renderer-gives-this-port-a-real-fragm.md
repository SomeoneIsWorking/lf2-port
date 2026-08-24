---
id: C020
kind: claim
status: falsified
created: 2026-08-06
tags: rendering,sdl,shaders
depends: runtime/video/hd2d.c, runtime/win32/win32.c
falsified_on: 2026-08-24
---

## Claim

SDL3's GPU renderer gives this port a real fragment-shader path, and it works headless. SDL_CreateRenderer(win, SDL_GPU_RENDERER) succeeds under SDL_VIDEODRIVER=offscreen with a Vulkan backend reporting shader format 0x2 (SPIR-V), so the lighting can be tested by frame dump with no display. The binding conventions a fragment shader must use: the texture being drawn is sampler set=2 binding=0, extra SDL_GPURenderState sampler_bindings follow at binding=1,2..., uniforms are set=3 binding=0, and the vertex stage supplies v_color at location 0 and v_uv at location 1. Extra samplers are bound as raw SDL_GPUTexture* taken from SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER. Per-draw uniform changes are safe: SDL_SetGPURenderStateFragmentUniforms flushes the queued draws first.

## Evidence

standalone probe ($CLAUDE_JOB_DIR/tmp/spike.c) against SDL 3.4.12: a fragment shader reading two samplers and one uniform rendered ff802280 -- red 0x80 from sampler 0, green 0x22 from sampler 1, blue 0x80 from the uniform -- while the SAME draw with no render state set rendered ff804020, the plain texture. Both classes run, so the positive is not a shader that happened to pass the texture through. Confirmed in the port: 'hd2d: vulkan backend, SPIR-V shaders loaded' and tools/e2e.sh render's light arm changes 8613 px.

## What would falsify it

an SDL release that renumbers the descriptor sets, or a host whose only GPU backend is Metal/D3D12 -- there SDL_GetGPUShaderFormats will not contain SPIR-V and hd2d_init refuses rather than approximating

## FALSIFIED 2026-08-24

A real macOS run reached SDL GPU through Metal, whose shader formats exclude the only shipped SPIR-V payloads; runtime/video/engine.c therefore refused the native engine and its character shading/cast-shadow passes exactly as this claim named as its falsifier.

> Anything that cited this claim as proof must be re-checked. Grep the repo for it.
