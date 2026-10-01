---
id: 69
title: The renderer and its effects are chosen by env vars; they must be in-game options
status: closed
symptom: LF2_ENGINE / LF2_HD2D / LF2_DOF are the only way to choose the renderer or switch the lighting and the defocus -- a player cannot find them, which by the project's own rule means they are not features.
tags: reported,renderer,engine,hd2d,feature,options
created: 2026-08-16
updated: 2026-09-09
---

## The constraint
The pause menu's Options page (runtime/app/pause.c, issue #37) is the port's only own UI and
already hosts the two light-angle rows; the renderer/effect rows belong beside them, live, over
the frozen frame. The environment overrides remain diagnostic arms for controlled A/B
comparisons meaning what they mean -- but unset, the menu state rules.
Effects are SDL3_GPU-specific (the engine), not injected into the game-driven SDL_Render path.
With the effects engine-only, the engine has to become the DEFAULT renderer or the default
picture loses its shading; the classic SDL_Render path stays selectable as the plain-picture
fallback and the byte-identity control arm.
