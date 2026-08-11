# Fonts

Two faces, shipped **unmodified**, and both are compiled into the binary rather than looked
up at runtime (`CMakeLists.txt` turns each into a byte array). There is no font search and no
fallback: a port whose text silently changes depending on what the host happens to have
installed is a port whose screenshots cannot be compared.

| File | Used for | Why this one |
|---|---|---|
| `LiberationSans-Regular.ttf` | the game's GDI text (`TextOutA`) | proportional, and metrically compatible with Arial — the face Windows would have used for a device context's default font, which is what the game's own layout was sized against |
| `LiberationMono-Regular.ttf` | the game's own 8×16 bitmap-sheet text | that text is drawn on a **fixed 8-pixel cell**, so it needs a monospaced face; a proportional one clips inside the cell |

## Licence

Both are **SIL Open Font License 1.1** — `LICENSE-Liberation.txt`, copied here as the licence
requires. Redistribution is explicitly permitted:

> Permission is hereby granted, free of charge, to any person obtaining a copy of the Font
> Software, to use, study, copy, merge, embed, modify, redistribute, and sell modified and
> unmodified copies of the Font Software […]

subject to the licence travelling with it (it does, here) and the Reserved Font Names not
being used for a *modified* version. These are unmodified copies, so that clause does not
bite. Digitized data copyright © 2010 Google Corporation; copyright © 2012 Red Hat, Inc.

## What this is not

This is not game content. `game/`, `LF2_v2.0a.exe` and everything extracted from them stay
gitignored and out of this repository — see the note in `docs/codemap.md`. A font the project
is licensed to redistribute is a different thing from an asset shipped with the game, and only
the second is forbidden here.

## Coverage, measured rather than assumed

Latin-only is enough, and that was checked before choosing: over a full route — front end,
character selection, the pre-fight overlay and a match — 16,375 `TextOutA` draws produced
**zero bytes above 0x7E**, the binary's entire `.rdata` literal pool contains no CJK, and all
24 character names and all 12 stage names in the game data are ASCII. Liberation Sans covers
85 of 85 distinct characters across both corpora.

If a mod ever puts CJK in front of this, the port will draw nothing for those codepoints and
say so rather than substituting silently — see `runtime/gdi.c`.
