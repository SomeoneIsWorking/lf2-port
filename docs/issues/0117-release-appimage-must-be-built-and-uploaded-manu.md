---
id: 117
title: Release AppImage must be built and uploaded manually
status: resolved
symptom: The repository contains a GitHub Actions AppImage workflow, but release artifacts must be produced and uploaded manually without committing generated output.
tags: reported,release,appimage
created: 2026-08-30
updated: 2026-08-30
---

## Root cause

The initial release implementation inferred hosted automation where the intended operator boundary
was a local reproducible build followed by a manual GitHub Release upload. The AppImage and generated
recompiler output are already ignored, but the tracked workflow violates that delivery contract.

## Required fix

Cancel the active hosted run, delete the workflow, make documentation name the local manual process,
build in a controlled older-Linux environment, verify the ignored artifact, upload it manually, and
commit only source, metadata, tests, and build tooling.

### Resolution (2026-08-30)
Cancelled the hosted run, deleted the GitHub Actions workflow, documented the local-only process, and kept the AppImage/recompiler output ignored. Built and gated v0.1.0 in a local Ubuntu 22.04 Clang 14 container, then manually created the GitHub Release and uploaded LF2-Port-x86_64.AppImage. GitHub reports the 14,436,856-byte asset uploaded with SHA-256 92fcbc4cdeab44650ea299019235268311225a9a942138b4c440e22035577356; a fresh download matched byte-for-byte.

After nested-ZIP setup and bounded extraction landed, built v0.1.1 from commit
`30f2292838f090364baca5c65c4d9852761fca15` and published the AppImage-only release. GitHub reports
the 16,226,808-byte asset with SHA-256
`d887a62aebd6778d96be70c11d80abde47eaa5166412460f8226bb421d06c7f7`, matching the locally gated
artifact. The APK remains deliberately absent until its signed-device release gates pass.
