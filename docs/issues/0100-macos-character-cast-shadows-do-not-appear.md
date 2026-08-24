---
id: 100
title: macOS: character cast shadows do not appear
status: investigating
symptom: On macOS the game runs, but the native HD2D character shadows are absent even though Character shading and shadows is enabled by default.
tags: reported,macos,rendering,hd2d,shadows
created: 2026-08-24
updated: 2026-08-24
---

## Root cause

`engine_init` required `SDL_GPU_SHADERFORMAT_SPIRV`, and both renderer shader factories
hardcoded SPIR-V with entry point `main`. SDL's Metal backend exposes MSL instead, so macOS
returned before any native colour, character-mask, cast-shadow, or light pipeline existed. The
working game was the SDL_Render fallback, which cannot produce the native cast shadows.

## What was tried / dead ends

The authoritative GLSL now generates both SPIR-V and MSL payloads for all eight shaders. A
shared selector gives Vulkan SPIR-V/`main` and Metal MSL/`main0`; the engine and standalone mesh
pass use the same selector. Two adjacent backend assumptions were removed as well: depth chooses
the first supported D32/D24/D16 target, and failure to wrap the lit GPU target for presentation
now refuses the target by name instead of returning success and drawing nothing.

Linux verification is complete: all 36 `ctest` checks passed, including the eight-payload offline
selector and regeneration gate. The serialized `tools/e2e.py visibility` route passed all nine
shipping-engine character/caster and earlier/equal/later shadow-depth arms on Vulkan.

## Resolution

Pending real-platform acceptance. A macOS run must report Metal with MSL shaders and a ready
engine, successfully wrap the lit target, and show or procedurally measure the cast shadows in a
match. Until that evidence exists this issue remains `investigating`, not resolved.
