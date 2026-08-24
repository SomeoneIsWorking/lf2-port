---
id: C059
kind: claim
status: holds
created: 2026-08-24
tags:
depends: runtime/video/gpu_shader_source.h#gpu_shader_source_select, tests/test_gpu_shader_source.c
---

## Claim

Every native-renderer shader has a nonempty MSL main0 payload selected on an MSL-only backend, while Vulkan continues to select SPIR-V main

## Evidence

tests/test_gpu_shader_source.c exercises the exact eight SPIR-V/MSL arrays included by engine.c and mesh.c: 33/33 selector checks passed; full ctest 36/36 passed; tools/e2e.py visibility passed all nine Vulkan shipping arms.

## What would falsify it

any authoritative GLSL shader changes without both generated payloads changing, gpu_shader_source fails for any of the eight shader pairs, or a Metal backend cannot create a shader/pipeline from a selected MSL payload
