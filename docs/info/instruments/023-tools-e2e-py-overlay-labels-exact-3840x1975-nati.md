---
id: I023
kind: instrument
status: DISTRUSTED
created: 2026-08-22
distrusted_on: 2026-08-25
---

## Instrument

tools/e2e.py overlay_labels: exact 3840x1975 native pre-fight panel and retained-original acceptance

## Validated by

2026-08-22 integrated three-arm run: real VS/Stage captures passed fixed 1092x596 rectangles and per-glyph/per-Latin native edge gates; blanked-CJK mutation lost coverage; synthesized logical-nearest kept coverage but all 20 CJK edge scores became zero; native-CJK/nearest-Latin hybrid kept CJK detail but all 12 Latin edge scores became zero; forced native append failure produced 0 appended/194 failures while original static label coverage remained visible.

## Known failure modes

(none recorded yet)

## DISTRUSTED 2026-08-25

Retired by issue #106: the port-authored overlay panel and overlay_labels route were deliberately deleted. The instrument was valid for that removed implementation but can no longer measure the shipping presentation.

> Every result this instrument produced is suspect until it is re-validated.
