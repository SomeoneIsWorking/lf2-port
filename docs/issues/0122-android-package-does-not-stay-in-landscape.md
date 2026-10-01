---
id: 122
title: Android package does not stay in landscape
status: closed
symptom: There is an existing sensorLandscape manifest declaration, but the installed APK was observed outside the intended landscape presentation.
tags: reported,android,orientation,release
created: 2026-08-31
updated: 2026-09-09
---

### Note (2026-08-31)
CAUSE 2026-08-31: SDLActivity reapplies orientation after SDL_CreateWindow; a resizable SDL window without SDL_HINT_ORIENTATIONS becomes FULL_USER and supersedes the manifest's sensorLandscape policy. runtime/platform/window_policy.c now sets LandscapeLeft/LandscapeRight before SDL_Init. The merged APK reports screenOrientation 0x6 (sensorLandscape); real-device rotation acceptance is still required.

### Note (2026-08-31)
REPORTED 2026-08-31: User reports the v0.1.5 APK still does not force landscape or fullscreen on-device. Existing emulator landscape evidence is insufficient; enforce both at the Android Activity lifecycle boundary and reproduce while system rotation is locked.
