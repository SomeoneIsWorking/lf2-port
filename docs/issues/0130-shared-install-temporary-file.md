---
id: 130
title: Two instances from one install corrupt each other's object data
status: open
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

Proper fix: the decrypted text never touches a shared path, e.g. the decrypt writes to a per-process
file or the parser decrypts in memory.
