---
id: 122
title: Android package does not stay in landscape
status: resolved
symptom: There is an existing sensorLandscape manifest declaration, but the installed APK was observed outside the intended landscape presentation.
tags: reported,android,orientation,release
created: 2026-08-31
updated: 2026-08-31
---

REPORTED 2026-08-31. The manifest already declares sensorLandscape, so repeating that declaration is not a fix. Reproduce which Activity/window state is portrait, trace SDL Activity orientation and configuration handling, enforce the shipping landscape contract at its actual owner, and add a package-level regression. Device verification remains part of issue #120.

### Note (2026-08-31)
CAUSE 2026-08-31: SDLActivity reapplies orientation after SDL_CreateWindow; a resizable SDL window without SDL_HINT_ORIENTATIONS becomes FULL_USER and supersedes the manifest's sensorLandscape policy. runtime/platform/window_policy.c now sets LandscapeLeft/LandscapeRight before SDL_Init. The merged APK reports screenOrientation 0x6 (sensorLandscape); real-device rotation acceptance is still required.

### Note (2026-08-31)
REPORTED 2026-08-31: User reports the v0.1.5 APK still does not force landscape or fullscreen on-device. Existing emulator landscape evidence is insufficient; enforce both at the Android Activity lifecycle boundary and reproduce while system rotation is locked.

### Resolution (2026-08-31)
SDL's post-window USER_LANDSCAPE write was the regression. After SDL_CreateWindow the native bridge invokes Lf2Activity.enforceLf2WindowPolicy(), which restores SENSOR_LANDSCAPE and inherited sticky immersive bars. A signed v0.1.6 APK installed over v0.1.5 on the shared API 35 emulator while the system was locked portrait; dumpsys reported SENSOR_LANDSCAPE, 2400x1080 ROTATION_90, and both status/navigation bars visible=false.
