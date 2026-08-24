---
id: 107
title: Native startup remains slow in the data object-registry parser
status: resolved
symptom: The custom native entry is genuine but boot still takes several seconds before the mode menu; LF2_STARTUP_TRACE measured about 5.6 seconds in the object-registry step while the other startup phases complete in about 0.17 seconds.
tags: reported,startup,performance,re,parser
created: 2026-08-24
updated: 2026-08-25
---

## Root cause

Two independent costs were conflated. The build had no `CMAKE_BUILD_TYPE`, so the 77,000-instruction
generated translation ran at `-O0`. In that build LF2's object-frame constructor crossed the
guest/host boundary for 466,462 `fscanf` calls. After that parser was replaced, 91,396 of the
remaining calls were a second byte-at-a-time decryptor used only by `stage.dat`. `dispatch()` also
looked up `LF2_WATCH` through `getenv` on every imported call.

## What was tried / dead ends

Optimizing drawing cannot remove token/import traffic: the measured parser spent only milliseconds
inside host `gscan`, with the rest in recompiled control flow and dispatch. Replacing the entire
object constructor would duplicate bitmap, sound, metadata, and weapon setup unnecessarily.

## Resolution

Development builds now explicitly use Clang `RelWithDebInfo`. `fn_0040ef70` keeps LF2's constructor
as a super-call over the metadata prefix and natively parses only the frame suffix, including every
scalar, point, hit/body record, sound reference, bounds, and LF2's peculiar EOF checksum behavior.
`fn_00414a30` shares the already-proven native decrypt implementation with `fn_004148a0`, and the
hot watch configuration is cached once. The profile fell from about 5.6 s in object-registry to
1.22 s of active data loading; scanner calls fell from 466,462 to 17,661, of which only 9 ms is
inside scanning. The remaining time is actual image/resource construction. The persistent
`object_parser` route compares 65 complete object blocks plus dynamic records, sounds, and the
cumulative checksum against the original parser with zero differing files. A separate slow/native
decrypt control produced 78 byte-identical plaintext files including `stage.dat`.

### Resolution (2026-08-25)
Made RelWithDebInfo explicit, ported only fn_0040ef70 frame suffix, ported stage.dat second decrypt entry, cached the import watch config, and extracted shared path/text translation. Scanner calls fell 466462 to 17661; active loading measures 0.779 s offscreen / 1.22 s visible, and 65-object plus 78-decrypt differential controls are exact.
