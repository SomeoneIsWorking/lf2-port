---
id: I006
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

tools/re/bg_table_check.py + LF2_BG_TABLE=all -- diffs the running game's background layer table (span/x/y/loop, every record in the registry) against every shipped bg.dat decrypted offline

## Validated by

Run against BOTH classes. Positive: 12 of 12 records matched, 152 layers, 0 unmatched files. Negative: --selftest feeds it a table whose loop field is wrong by one value and asserts it FAILS -- without that, 'ok' and 'my parser produced two empty sides' are the same output. It caught its own first version doing exactly that: the y regex matched the 'y: 0' inside 'transparency: 0', so every stage's y read as 0/1 and every comparison failed for a reason unrelated to the table. Refuses with exit 2, saying it compared NOTHING, when the log has no bg-table lines or the game tree has no bg.dat -- a missing corpus must not read as a pass. LF2_BG_TABLE=all is what makes the check possible at all: VS mode picked the same stage on six consecutive headless runs, so re-running for a second stage does not work, while the registry holds all twelve at once.

## Known failure modes

(none recorded yet)
