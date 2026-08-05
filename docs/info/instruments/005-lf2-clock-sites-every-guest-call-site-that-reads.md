---
id: I005
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

LF2_CLOCK_SITES -- every guest call site that reads the clock, with its total reads and its longest RUN of reads with no Sleep between them (runtime/imports.c)

## Validated by

Run against BOTH classes before being believed. Variable unset: the run prints nothing at all (0 matching lines). Variable set: a 2400-frame route named 14 call sites and 489,218 reads. It also refuses rather than printing an empty list -- a run that read the clock zero times says so explicitly instead of showing a blank table that reads as 'there is no spin'. The RUN column is what makes it discriminate: call count alone cannot separate a well-behaved deadline loop (reads constantly, sleeps between reads) from a spin, and the two hot sites came out at reads=241,581 longest-run=401 against a handful for every other site.

## Known failure modes

(none recorded yet)
