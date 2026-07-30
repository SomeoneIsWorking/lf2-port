---
id: 1
title: Local game/data ad files clobbered by runtime ad updater (Wine runs)
status: resolved
symptom: ads absent locally but present on a fresh install; adinfo.txt reads 'now 0 4'; ad1.txt contains '500 Internal Server Error'
tags: ads,data,verification,trap
created: 2026-07-30
updated: 2026-07-30
---

**Cause:** the game's ad system rewrites data/adinfo.txt and ad1.txt at runtime; under Wine the fetch reached the dead ad server and wrote its error page over the banner list. Any 'ads are gone' verification against such a tree is testing missing data, not the overrides.

**Resolution:** re-verified ad removal against a PRISTINE tree extracted from the installer (tools/extract_game.py): menu frame 400 is clean under both gcc and clang on x86, and no ad blit rect appears. Baseline distinct RECT set on the main menu (LF2_BLT_RECTS=1, 500 frames): (0,0,257,541) (0,0,0,0) (155,96,651,176) (263,202,545,383) (0,2,11,21) (0,2,0,2) (0,0,794,550) — the char-art rect width varies with the character drawn. Anything beyond this set on another machine is the thing being drawn there and not here.

**Open:** macOS/arm64 reportedly shows ads with identical source, generated code and data — not yet diagnosed; needs the RECT set from the Mac.

### Note (2026-07-30)
RESOLVED root cause: the loading screen and mode menu had never-removed ad presenters (loading grid + advertise link via fn_004242e0; mode-menu panel = element 0x0044d020, distinct from the front menu's 0x0044d060). Invisible locally because adinfo.txt was factory-reset. Root fix: override fn_0043c4a0 (ad-set load) to return 0 -> init falls to fn_0043c690 (factory reset, loads nothing); advertise link declined at its single call site 0x0042459a in fn_0043f010. Near misses: fn_0043c240 parses ad files but never runs at boot; fn_0043bec0 is the DirectDraw init check, NOT ad code.
