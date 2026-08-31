---
id: 127
title: Portable game-state snapshots across ports
status: open
symptom: Android must preserve a running game through app switches and process recreation.
tags: reported,state,serialization,android,shared,ports
created: 2026-08-31
updated: 2026-08-31
---

REPORTED: LF2 currently relies on in-memory SDLActivity state; it has no durable guest-machine snapshot. Define and implement one shared, versioned, identity-bound snapshot container with title-owned sections and a safe-point lifecycle API usable by LF2, X-Men 2, PSX, and future ports. Do not serialize host pointers, GPU objects, audio streams, OS handles, or user game assets; each title restores deterministic guest/device state and rebuilds derived host resources. Normal Home/resume must retain the existing process without serializing; durable snapshots are the recovery path only after process death.
