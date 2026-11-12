---
id: 130
title: Two instances from one install corrupt each other's object data
status: resolved
symptom: Two LF2s started together from the same game directory abort with "object parser: malformed frame data", or desync within the first frames of a session.
state_items: S014, S023
tags: netplay,data,files
created: 2026-11-12
updated: 2026-11-12
---

The game decrypts every `.dat` into `data\temporary.txt` inside the install and parses it back
(`runtime/overrides/object_parser.c:fn_0040ef70`). Two processes loading at the same moment from one
install read each other's half-written file. Online sessions now boot both peers together, so
`tools/routes/online_meeting_test.py` hit it in about a third of runs; the test now gives each instance
its own copy of the install. Players on separate machines are unaffected; two copies on one machine
from one install still collide.

Cause: `runtime/win32/paths.c:translate_path` resolved the guest's `data\temporary.txt` to the
one file in the install, so every process decrypted into and parsed back the same bytes.

Fix: `translate_path` resolves that one guest path to `data/temporary.<pid>.txt`, so the decrypt, the
native parser and the guest's own CRT reads all use a file of the process's own; an `atexit` hook
deletes it. `tools/routes/determinism_test.py` now starts its two runs together from one install and
gets identical state on every frame, with the shared `temporary.txt` left untouched.
