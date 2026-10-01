---
id: 119
title: AppImage packaging does not preflight the file utility
status: closed
symptom: The manually gated AppImage build reaches appimagetool and fails late because the Ubuntu container lacks the file command.
tags: release,appimage
created: 2026-08-30
updated: 2026-09-09
---

## Root cause
The packager verifies its downloaded AppImage tools and build inputs but not the native `file`
utility required by appimagetool. The old hosted prerequisite list happened to include it
transitively on some runners, so the true local contract was incomplete.
