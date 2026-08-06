---
id: I009
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

catalog.py list --tag reported --status open -- the command CLAUDE.md defines as THE QUEUE

## Validated by

CAUGHT LYING, and the lie was silent and total. It reported 3 open reported issues when there were 5: #36 and #39 were filed, on disk, open, and invisible to it.

CAUSE, in the tool and not in this project: catalog.py's add takes --tags (plural) with no append action, and argparse PREFIX-MATCHES --tag onto it. So 'add ... --tag reported --tag widescreen --tag audio --tag re' kept only 're'. Nine entries were filed that way in one session and every one lost its 'reported' tag, which is exactly the tag the queue filters on. Nothing warned; the entries looked fine in 'list' and in 'show'.

FIXED IN THE TOOL: --tags now takes --tag as an explicit alias with action='extend', so repeats accumulate. Proven on both classes -- 'add probe --tag alpha --tag beta --tags gamma,delta' now writes 'tags: alpha,beta,gamma,delta' where before it wrote 'tags: delta'. The nine affected entries had their tags restored by hand and the queue now lists 5.

WHAT TO DISTRUST UNTIL RE-CHECKED: any conclusion drawn from a catalog query in a session that filed entries with repeated --tag. 'No open issues match' from this tool is only trustworthy once the entries' own tags lines have been read. The failure mode is that a FILED issue becomes a LOST issue, which is the one thing the catalog exists to prevent.

## Known failure modes

(none recorded yet)

### Second failure mode, same session (2026-08-07)

IT LIED AGAIN, differently. Run from `scratch/build` -- a subdirectory of the very repo whose
queue it was being asked about -- `list --tag reported --status open` printed "(no entries)"
and exited 0. Three issues were open. The default catalog dir is RELATIVE (`docs/issues`), so
any `cd` into a build or tools directory silently turns the whole registry into an empty one.

This is the same class as the tag bug and worse: a missing corpus reported as an empty result.
It was caught only because the answer contradicted something read a minute earlier -- there is
no version of "(no entries)" that looks wrong on its own.

FIXED: `_load_all` now refuses a missing directory, printing the absolute path it searched and
exiting 2, instead of returning `[]`. `add` is untouched and still creates a catalog, because
bootstrapping one is a legitimate write; only READS refuse. Proven on both classes -- exit 2
with the explanation from `/tmp`, and the correct 3 entries from the repo root.

THE STANDING RULE THIS EARNS: an empty answer from a search tool is only believable once you
know what corpus it searched. Two independent mechanisms in one tool turned "I found nothing"
into "there is nothing", and both were silent.
