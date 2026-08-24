# RE Frontier — LF2 native-port replacement boundaries

This is the ordered ground-truth chain behind native replacements. `docs/codemap.md` says what
exists; this file distinguishes binary-derived ports from authored host behavior. A
`re-verified` step has both binary/data evidence and a real-game falsifier.

Statuses: `re-verified`, `re-partial`, `in-progress`, `hack`, `authored`, `todo`, and
`skip-by-design`. A `hack` is debt and cannot be a resting state.

<!-- Machine-edited by re_frontier.py add/set. Format: `## <area>` sections;
     each entry is `### <id> — <title>` followed by `- <field>: <value>` lines. -->

## startup

### START-ENTRY — Native process entry
- status: re-verified
- deps:
- evidence: PE initializer table and decompiled entry/WinMain call graph; smoke requires the native-entry marker and rejects guest PE-entry/WinMain markers
- where: runtime/app/port_entry.c, tools/routes/smoke_test.py
- gap:
- notes: PE loading remains runtime-owned; the guest CRT entry and WinMain are not dispatched.

### START-INIT — RE-ported bounded initialization phases
- status: re-verified
- deps: START-ENTRY
- evidence: decompiled frontend and mode-2 initializer branches aligned to nine named phases; smoke reaches modemenu first and match with every anchored input fired
- where: runtime/overrides/startup_init.c, runtime/overrides/startup_frontend.c, runtime/overrides/startup_world.c
- gap:
- notes: Cocoa events are pumped only between phases; no game update or music handoff runs early.

### START-OBJECTS — Native object-frame parser
- status: re-verified
- deps: START-INIT
- evidence: object_parser route compares 65 complete objects, dynamic attack/body records, sounds, and cumulative checksum byte-for-byte with fn_0040ef70
- where: runtime/overrides/object_parser.c, runtime/overrides/object_frames.c, tools/routes/object_parser_test.py
- gap:
- notes: LF2 remains authoritative for the constructor prefix, bitmap/resource setup, and allocation.

## behavior

### CHEATS — Built-in hidden roster and F6-F9 actions
- status: re-verified
- deps: START-INIT
- evidence: decompiled F3/F6-F9 leaves define gates and side effects; cheats route observes LF2's own F6 counter and unlimited-MP word change exactly once
- where: runtime/overrides/cheats.c, runtime/app/function_keys.c, tools/routes/cheats_test.py
- gap:
- notes: Removing the original lock/mode gates is an intentional port policy; action semantics remain binary-derived.

## presentation

### OVERLAY-ORIGINAL — Original pre-fight CHARMENU
- status: re-verified
- deps: START-INIT
- evidence: LF2's retained CHARMENU bitmap is the sole panel producer in a 1920x1080 forced-overlay capture
- where: runtime/overrides/text.c, runtime/video/ddraw.c
- gap:
- notes: The port-authored replacement panel and its duplicate font/assets were removed.
