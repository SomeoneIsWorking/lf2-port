---
id: C036
kind: claim
status: holds
created: 2026-08-13
tags: renderer,stage
depends: tests/test_stage_assets.c, runtime/video/stagegeom.c
---

## Claim

All twelve shipped Stage Mode backgrounds have committed original 3D scenes whose model paths and layer anchors load successfully.

## Evidence

ctest stage_assets loads every committed scene through the shipping stagegeom loader with each scenes recorded bg.dat layer spans; 12/12 passed on 2026-08-13.

## What would falsify it

a stage file, shared OBJ model, loader, or bg.dat layer naming changes; rerun ctest stage_assets and the stage-geometry route
