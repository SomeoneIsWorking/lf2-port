---
id: 117
title: Release AppImage must be built and uploaded manually
status: investigating
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
