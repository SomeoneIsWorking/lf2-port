---
id: 109
title: Visibility probe sample copy reads past source arrays
status: resolved
symptom: GCC reports memcpy reading eight integers from six-integer visibility sample arrays
tags: diagnostics,renderer,undefined-behavior
created: 2026-08-25
updated: 2026-08-25
---

## Root cause\n\nThe visibility probe owns an eight-integer destination because its largest arm samples four points, but the two- and three-point arms declare six-integer source arrays and copied with sizeof(destination). That reads two integers beyond each source even though those destination elements are not consumed.\n\n## Constraint\n\nCopy each arm's actual source extent and initialize the shared destination; do not suppress the GCC bounds warning.\n\n## Resolution\n

### Resolution (2026-08-25)
Zero-initialize the shared eight-integer destination and copy each arm with sizeof(source), so two-/three-point arms never read beyond their six-integer samples. GCC rebuilt the shipping target without the bounds warning and the complete Clang suite passed.
