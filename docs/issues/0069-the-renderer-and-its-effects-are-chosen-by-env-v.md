---
id: 69
title: The renderer and its effects are chosen by env vars; they must be in-game options
status: resolved
symptom: LF2_ENGINE / LF2_HD2D / LF2_DOF are the only way to choose the renderer or switch the lighting and the defocus -- a player cannot find them, which by the project's own rule means they are not features.
tags: reported,renderer,engine,hd2d,feature,options
created: 2026-08-16
updated: 2026-08-16
---

## Reported

The user, while asking for the shading on the engine: "don't use flags to control renderer, add
ingame options" and "the rendering effects such as individual shaders should be renderer
options". Env vars are for DIAGNOSTICS, never for features (AGENTS.md); the renderer choice and
the two effects were flag-only, and LF2_ENGINE was even latched at first read.

## The constraint

The pause menu's Options page (runtime/app/pause.c, issue #37) is the port's only own UI and
already hosts the two light-angle rows; the renderer/effect rows belong beside them, live, over
the frozen frame. The environment overrides remain diagnostic arms for controlled A/B
comparisons meaning what they mean -- but unset, the menu state rules.

Effects are SDL3_GPU-specific (the engine), not injected into the game-driven SDL_Render path.
With the effects engine-only, the engine has to become the DEFAULT renderer or the default
picture loses its shading; the classic SDL_Render path stays selectable as the plain-picture
fallback and the byte-identity control arm.

### Resolution (2026-08-16)
The renderer and its effects are now in-game options. runtime/app/options.{c,h} holds the state (engine/lighting/DOF, default engine); the pause menu's Options page owns it with three rows (RENDERER/LIGHTING/DEPTH OF FIELD) beside the two light-angle rows; engine.c/hd2d.c read the options rather than the env vars, so a change applies to the next frame. The LF2_ENGINE/LF2_HD2D/LF2_DOF env vars remain honoured ONCE as the initial value, so the route scripts' A/B arms keep their meaning. The lighting moved engine-only, which is why the engine became the default renderer. the recorded render runtime scenario now pins the classic path with LF2_ENGINE=0 explicitly. Unit suite 13/13.
