---
id: 95
title: Replace every README promotional screenshot with fresh current-build captures
status: resolved
symptom: The repository's existing promotional screenshot set must be removed and replaced with newly captured screenshots from the current port
tags: reported,docs,readme,screenshots,showcase
created: 2026-08-22
updated: 2026-08-22
---

## Reported

USER 2026-08-22: "Ok, remove all screenshots from the repo and upload new ones"

Remove the current project-owned screenshot gallery under `docs/screenshots/` and replace it with a fresh, coherent set captured from the current shipping build. Update every README/document reference atomically so no removed image remains linked. Do not delete unrelated vendored dependency art such as RmlUi sample assets. Captures must be intentional promotional images, not a raw frame-dump corpus, and must not add extracted game assets or executables.

## Resolution

The five old tracked PNGs were removed and replaced under new filenames by one successful run of
`tools/capture_readme_gallery.py` against the current Clang build. The tool serially captured active
Demo combat, the GAME/GRAPHICS/CONTROLS RmlUi pages at 1920x1080, and Stage 1-1 PvE at 3440x1440.
It retained raw PPMs and logs under `scratch/readme_gallery/`, validated dimensions before and after
encoding-only PNG conversion, and never wrote the tracked gallery directly.

All five candidates were inspected at native resolution before promotion. README references were
updated atomically, the old filenames have no live references, and the vendored RmlUi gitlink was
left untouched.
