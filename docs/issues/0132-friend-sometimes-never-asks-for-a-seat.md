---
id: 132
title: The friend sometimes never asks a listed game for a seat
status: open
symptom: One run of four of online_meeting_test.py --zerotier ended with the friend never asking (no [lobby] line) and never joining; the friend's pad pressed JOIN at modemenu+500.
state_items: S022
tags: netplay,lobby,test
created: 2026-11-12
updated: 2026-11-12
---

The host had listed its game (invite at 10:00:22, listing switched on before it) and logged no
directory failure; the friend opened the ONLINE page at 10:00:27 and logged nothing after the
ZeroTier node started. Either row 0 was not yet on the friend's list when its pad pressed JOIN,
or the ask failed on a path that did not log. Both failure paths of `LobbyList::start_ask` and
`poll_ask` now log, and the test keeps the friend's frame at modemenu+495, so the next occurrence
shows which.
