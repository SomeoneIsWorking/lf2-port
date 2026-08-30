---
id: 119
title: AppImage packaging does not preflight the file utility
status: resolved
symptom: The manually gated AppImage build reaches appimagetool and fails late because the Ubuntu container lacks the file command.
tags: release,appimage
created: 2026-08-30
updated: 2026-08-30
---

## Root cause

The packager verifies its downloaded AppImage tools and build inputs but not the native `file`
utility required by appimagetool. The old hosted prerequisite list happened to include it
transitively on some runners, so the true local contract was incomplete.

## Required fix

Fail before staging with exact Ubuntu and Fedora install commands when `file` is absent, document it
as a manual release prerequisite, and rerun packaging in the controlled Ubuntu container.

### Resolution (2026-08-30)
Added an appimage.py preflight for the file utility with exact apt and dnf commands plus a negative unit test. Installed file in the local Ubuntu 22.04 release container; packaging and all three content scans then passed.
