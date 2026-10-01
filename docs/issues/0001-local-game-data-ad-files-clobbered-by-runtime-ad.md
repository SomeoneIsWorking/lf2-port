---
id: 1
title: Local game/data ad files clobbered by runtime ad updater (Wine runs)
status: closed
symptom: ads absent locally but present on a fresh install; adinfo.txt reads 'now 0 4'; ad1.txt contains '500 Internal Server Error'
tags: ads,data,verification,trap
created: 2026-07-30
updated: 2026-09-09
---

### Note (2026-07-30)
RESOLVED root cause: the loading screen and mode menu had never-removed ad presenters (loading grid + advertise link via fn_004242e0; mode-menu panel = element 0x0044d020, distinct from the front menu's 0x0044d060). Invisible locally because adinfo.txt was factory-reset. Root fix: override fn_0043c4a0 (ad-set load) to return 0 -> init falls to fn_0043c690 (factory reset, loads nothing); advertise link declined at its single call site 0x0042459a in fn_0043f010. Near misses: fn_0043c240 parses ad files but never runs at boot; fn_0043bec0 is the DirectDraw init check, NOT ad code.
