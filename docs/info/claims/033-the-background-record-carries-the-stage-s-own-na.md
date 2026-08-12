---
id: C033
kind: claim
status: holds
created: 2026-08-12
tags: 
---

## Claim

The background record carries the stage's own name and each layer's bitmap path

## Evidence

fn_0040c160, the bg.dat parser, scans name: into base+0x4d4617c (30 bytes, every '_' replaced by ' ') and each layer's path into base+0x4d45dd0+n*30, where base = world + index*0x990. LF2_BG_TABLE=all against tools/re/bg_table_check.py: 12 of 12 runtime records matched a bg.dat by geometry AND by name, 157 layer names compared, 0 mismatches. The same pass located bg.dat's perspective: at base+0x4d45dbc/+0x4d45dc0 (no shipped stage sets it) and corrected BG_LAYER_PIC, which is bg.dat's transparency: -- fn_0043f010 uses that argument as -(arg!=0)&0x8000, a colour-key enable.

## What would falsify it

a stage whose record name or layer path disagrees with its bg.dat, or a bg_table_check run that matches fewer than 12 of 12
