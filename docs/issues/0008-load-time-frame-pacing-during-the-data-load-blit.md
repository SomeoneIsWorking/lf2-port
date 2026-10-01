---
id: 8
title: Load time: frame pacing during the data load; blit throttling is a dead end
status: closed
symptom: loading takes ~10-15 s; optimising parsing and import dispatch changed nothing
tags: load,performance,pacing,rendering,dead-end
created: 2026-08-05
updated: 2026-09-09
---

### Note (2026-08-24)
2026-08-24 correction: the failed repeated-drive experiments do not prove the actual data initializer is time-gated. fn_004246b0 mode 1 only presents the loading picture and changes top mode to 2; fn_0043e9a0 does not establish the one-shot load gate. The real synchronous initializer is fn_0041bc90's 0x0041be98..0x0041c57b branch when 0x0044d05c == 1. Repeatedly calling the two wrong functions loaded zero files because neither owned that branch.
