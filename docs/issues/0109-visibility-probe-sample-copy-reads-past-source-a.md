---
id: 109
title: Visibility probe sample copy reads past the source array
status: closed
symptom: GCC bounds warning in the visibility probe's sample copy
tags: bug,video,probe
created: 2026-08-25
updated: 2026-09-09
---

Root cause: the probe owns an eight-integer destination because its largest arm samples
four points, but the two- and three-point arms declare six-integer source arrays and copied
with `sizeof(destination)`, reading two integers past each source even though those
destination elements are never consumed.

Fix: zero-initialize the shared destination and copy each arm with `sizeof(source)`. Do not
suppress the GCC bounds warning. The full record is in git history.
