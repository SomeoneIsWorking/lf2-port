# Fonts

Three embedded faces are compiled into the binary rather than looked up at runtime
(`CMakeLists.txt` turns each into a byte array). The two Liberation faces are shipped
**unmodified**; the CJK face is the documented subset below. There is no font search and no
fallback: a port whose text silently changes depending on what the host happens to have
installed is a port whose screenshots cannot be compared.

| File | Used for | Why this one |
|---|---|---|
| `LiberationSans-Regular.ttf` | the game's GDI text (`TextOutA`) and the Latin runs in the native pre-fight panel | proportional, and metrically compatible with Arial — the face Windows would have used for a device context's default font, which is what the game's own layout was sized against |
| `LiberationMono-Regular.ttf` | the game's own 8×16 bitmap-sheet text | that text is drawn on a **fixed 8-pixel cell**, so it needs a monospaced face; a proportional one clips inside the cell |
| `DroidSansFallback-LF2OverlaySubset.ttf` | Traditional Chinese runs in the native pre-fight panel | a 6,260-byte subset containing exactly the panel's 17 required codepoints; it avoids a system-font dependency without embedding the source face's full 4 MB CJK corpus |

## Licences and modification notice

The two Liberation faces are **SIL Open Font License 1.1** — `LICENSE-Liberation.txt`, copied here as the licence
requires. Redistribution is explicitly permitted:

> Permission is hereby granted, free of charge, to any person obtaining a copy of the Font
> Software, to use, study, copy, merge, embed, modify, redistribute, and sell modified and
> unmodified copies of the Font Software […]

subject to the licence travelling with it (it does, here) and the Reserved Font Names not
being used for a *modified* version. These are unmodified copies, so that clause does not
bite. Digitized data copyright © 2010 Google Corporation; copyright © 2012 Red Hat, Inc.

**Modification notice:** `DroidSansFallback-LF2OverlaySubset.ttf` is a **modified subset**,
not an unmodified Droid release. Its internal family, full, PostScript, unique-ID, and version
names identify it as the LF2 Overlay Subset. It was made from Android's
`DroidSansFallbackFull.ttf` with fontTools. Copyright © 2005–2008 The Android Open Source
Project; it is distributed under Apache License 2.0, whose complete notice and terms travel
in `LICENSE-DroidSansFallback.txt`. Deleting every unused glyph and changing the internal
names are the modifications.

The exact source used was Fedora package `google-droid-sans-fonts-20200215-24.fc44`, whose
declared upstream is `https://android.googlesource.com/`. The source file SHA-256 is
`2392015530438bafc48edfc4aee6d9de2387f627a6134d8ab3dfcc99d21c8240`. Regenerate the modified
font from that source with:

```sh
python3 tools/build/subset_overlay_font.py \
  /usr/share/fonts/google-droid-sans-fonts/DroidSansFallbackFull.ttf \
  assets/fonts/DroidSansFallback-LF2OverlaySubset.ttf
```

The tool reads the actual CJK literals in `runtime/ui/overlay_panel.c`, subsets to that set,
and writes the internal modification names; there is no second hand-maintained glyph manifest.
The committed subset SHA-256 is
`fd522ad8011225605d98bbeaba5b601c899970c95cce7d2200b53adae1caa072`. Name records 7–14
retain the source's trademark, vendor/designer attribution, URLs, and Apache licence notices;
the font test refuses an asset that strips them.

`tests/test_overlay_font.py` reads the committed font's format-4 `cmap` without consulting
fontconfig or a system face. It resolves every actual glyph ID and requires a nonempty `glyf`
outline for all and only the runtime label codepoints:

```
卡始度擇新景機背色角選重開關隨離難
```

That covers `開始`, `重新選擇角色`, `重新隨機角色`, `背景`, `難度`, `離開`, and `關卡`.
There is no missing-glyph fallback: if either embedded panel face cannot open, the complete
original bitmap panel is retained rather than mixing sharp and baked labels.

## What this is not

This is not game content. `game/`, `LF2_v2.0a.exe` and everything extracted from them stay
gitignored and out of this repository — see the note in `docs/codemap.md`. A font the project
is licensed to redistribute is a different thing from an asset shipped with the game, and only
the second is forbidden here.

## Coverage, measured rather than assumed

Latin-only is enough for the game's two dynamic text paths, and that was checked before
choosing their faces: over a full route — front end, character selection, the pre-fight overlay
and a match — 16,375 `TextOutA` draws produced
**zero bytes above 0x7E**, the binary's entire `.rdata` literal pool contains no CJK, and all
24 character names and all 12 stage names in the game data are ASCII. Liberation Sans covers
85 of 85 distinct characters across both corpora. The native panel is a separate port-owned
UTF-8 layout, so its deliberately restored Traditional Chinese labels use the subset above.

If a mod ever puts CJK in front of this, the port will draw nothing for those codepoints and
say so rather than substituting silently — see `runtime/win32/gdi.c`.
