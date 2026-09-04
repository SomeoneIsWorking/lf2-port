---
id: 120
title: Make Android a working native release target
status: open
symptom: There is no installable Android build with first-run game import, touch controls, or device verification
tags: reported,android,release,input
created: 2026-08-30
updated: 2026-08-30
---

The Android ARM64 product path now builds one NDK 28 `libmain.so`, exports SDL's `main` entrypoint, and links the translated program with NEON-enabled PIC FFmpeg. Its SDL Activity uses SAF to copy a complete selected folder or ZIP into bounded app-private staging; the native Lucent-backed selection path finds one nested `lf2.exe`, validates the complete referenced tree, and commits only a validated import. Transient SAF access is sufficient because no external URI remains in use. Multi-touch routes through Lucent's contact router and LF2's existing action/key ledger, with safe-area layout, multi-touch reference counts, pause/lifecycle cancellation, a persistent visibility option, and controller suppression. Settings, imported game files, and package resources use app-private storage. The release tool refuses unsigned output, inspects package content, and verifies an assembled release signature.

Publication is still blocked on external release evidence: release signing credentials are not configured and no ADB device is connected. The local package shell pins Gradle 9.7.1 and Android Gradle Plugin 9.3.0 and runs on the host's Java 25, but its gameplay payload must be replaced by the ARM64 `x86port` JIT product before another package is qualified. After signing credentials and hardware are supplied, the signed APK must prove that it contains no interpreter execution, interpreter-backed helper, fallback machinery, or offline-generated guest corpus and pass first-run direct-folder and nested-ZIP import, touch and physical-controller switching, WMA/sound playback, suspend/resume and Activity recreation, renderer correctness, loading and memory checks, and sustained frame-time/thermal measurements on named devices. Package output remains ignored and GitHub Actions is not permitted.
