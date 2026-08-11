---
id: I011
kind: instrument
status: trusted
created: 2026-08-11
---

## Instrument

LF2_HD2D=off -- the HD2D lighting switch, used as a control

## Validated by

2026-08-11: NOT a control for any single lighting term. Diffing a frame against LF2_HD2D=off and calling the difference 'the cast shadow' reported 1837-2245 px at 1920x1080 where the shadow alone is 271-713 -- the rest is the key light, the bevel and the floor tint. The per-term control is the knob: LF2_HD2D_SHADOW=0 against the default isolates the cast shadow with the rest of the pass still running, and LF2_HD2D_SHOW=chars gives the character mask to test pixels against. Caught by the contradiction that 1508 'shadow' pixels lay on characters while only 620 of the shadow MASK overlapped the character mask

## What it does

`LF2_HD2D=off` disables the whole HD2D pass: the key light, the hemisphere ambient, the bevel,
the floor tint AND the cast shadows. `runtime/video/render.c` then presents the composition as
the game drew it.

## Known failure modes

### IT IS A CONTROL FOR "IS THE PASS RUNNING", NOT FOR ANY TERM INSIDE IT

Diffing a lit frame against an `LF2_HD2D=off` frame and attributing the difference to one term
is wrong, and it is wrong in a way that looks right: most of a fighter's body changes under
that diff, so a marked-up frame reads as "the shadow is all over the fighter" whatever the
shadow is actually doing.

    the whole pass       1837-2245 px changed on a match frame at 1920x1080
    the cast shadow      271-713 px

### THE PER-TERM CONTROLS

- `LF2_HD2D_SHADOW=0` against the default — the cast shadow alone, with the rest of the pass
  still running. This is the one that answers "which pixels does the shadow darken".
- `LF2_HD2D_KEY`, `LF2_HD2D_AMBIENT`, `LF2_HD2D_BEVEL` — the same shape for the other terms.
- `LF2_HD2D_SHOW=albedo|chars|shadow` presents that buffer instead of the lit frame, which is
  how a pixel is tested for belonging to a character rather than guessed at from a screenshot.

### HOW THE MISUSE WAS CAUGHT

Not by noticing the switch was wrong — by a contradiction in the numbers. 1508 supposedly
shadowed pixels lay inside the character mask while only 620 pixels of the shadow MASK
overlapped that mask at all. A term cannot darken more pixels than it covers.
