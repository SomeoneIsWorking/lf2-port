---
id: 107
title: Native startup remains slow in the data object-registry parser
status: closed
symptom: The custom native entry is genuine but boot still takes several seconds before the mode menu; LF2_STARTUP_TRACE measured about 5.6 seconds in the object-registry step while the other startup phases complete in about 0.17 seconds.
tags: reported,startup,performance,re,parser
created: 2026-08-24
updated: 2026-09-09
---

## Root cause
Two independent costs were conflated. The build had no `CMAKE_BUILD_TYPE`, so the 77,000-instruction
generated translation ran at `-O0`. In that build LF2's object-frame constructor crossed the
guest/host boundary for 466,462 `fscanf` calls. After that parser was replaced, 91,396 of the
remaining calls were a second byte-at-a-time decryptor used only by `stage.dat`. `dispatch()` also
looked up `LF2_WATCH` through `getenv` on every imported call.

## What was tried / dead ends
Optimizing drawing cannot remove token/import traffic: the measured parser spent only milliseconds
inside host `gscan`, with the rest in guest control flow and dispatch. Replacing the entire
object constructor would duplicate bitmap, sound, metadata, and weapon setup unnecessarily.
