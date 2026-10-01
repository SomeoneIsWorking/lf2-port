---
id: 95
title: Replace every README promotional screenshot with fresh current-build captures
status: closed
symptom: The repository's existing promotional screenshot set must be removed and replaced with newly captured screenshots from the current port
tags: reported,docs,readme,screenshots,showcase
created: 2026-08-22
updated: 2026-09-09
---

## Reported
USER 2026-08-22: "Ok, remove all screenshots from the repo and upload new ones"
Remove the current project-owned screenshot gallery under `docs/screenshots/` and replace it with a fresh, coherent set captured from the current shipping build. Update every README/document reference atomically so no removed image remains linked. Do not delete unrelated vendored dependency art such as RmlUi sample assets. Captures must be intentional promotional images, not a raw frame-dump corpus, and must not add extracted game assets or executables.
