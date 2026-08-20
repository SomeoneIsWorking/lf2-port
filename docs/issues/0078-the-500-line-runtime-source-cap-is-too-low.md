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

The original gate mistook a very small line count for an ownership boundary. Dusklight's current
host tree contains cohesive modules between roughly 800 and 1,964 lines, so 500 rejects valid module
growth before it indicates a god file.

## What was tried / dead ends

Keeping per-file caps for every file that happened to exceed 500 would preserve the same mistaken
policy under a legacy exception table. Those entries were removed rather than retained as hidden
sub-1,200 limits.

## Resolution

### Resolution (2026-08-20)
The cap was an overcorrection: Dusklight's cohesive host modules commonly exceed 500 lines, including files around 800-1964 lines. The project and global port policy now use a 1200-line default cap, freeze LF2's three existing files above it at their current counts, and classify 2000+ lines as critical extraction territory. The structure gate passed and a negative control proved it rejects one-line legacy growth.
