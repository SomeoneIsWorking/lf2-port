---
id: 78
title: The 500-line runtime source cap is too low
status: resolved
symptom: The mechanical structure gate treats cohesive files above 500 lines as monoliths even though the requested limit is 1200 and architectural danger starts around 2000 lines
tags: reported,workflow,structure
created: 2026-08-20
updated: 2026-08-20
---

## Root cause

The original gate mistook a very small line count for an ownership boundary.
Five hundred lines rejected cohesive modules before it indicated a god file.

## What was tried / dead ends

Keeping per-file caps for every file that happened to exceed 500 would preserve the same mistaken
policy under a legacy exception table. Those entries were removed rather than retained as hidden
sub-1,200 limits.

## Resolution

### Resolution (2026-08-20)
The project policy now uses a 1200-line default cap, freezes LF2's three
existing files above it at their current counts, and classifies 2000+ lines as
critical extraction territory. The structure gate passed and a negative
control proved it rejects one-line legacy growth.
