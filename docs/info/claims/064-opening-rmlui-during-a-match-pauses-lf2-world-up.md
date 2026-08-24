---
id: C064
kind: claim
status: holds
created: 2026-08-25
tags: rmlui
depends: runtime/app/pause.c#open_menu, runtime/app/pause.c#pause_menu_close, tools/routes/ui_global_test.py
---

## Claim

Opening RmlUi during a match pauses LF2 world updates while the document renders, and closing resumes from the exact prior pause pipeline state.

## Evidence

2026-08-25 tools/e2e.py ui_global: outside-document world pixels stayed byte-identical while open, matching no-modal control changed 0.454590, and the first two post-close frames changed 0.006576 and 0.003658.

## What would falsify it

World pixels change while the match modal is open, RmlUi stops rendering, or world pixels fail to resume immediately after close.
