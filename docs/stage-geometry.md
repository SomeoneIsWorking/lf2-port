# Hand-woven stage geometry

The format the port loads authored 3D sets from, and the reasoning behind every choice in it.
Issue #62. Nothing here is game content: a `.stage` file and the models it names are the
port's own work and are committed to the repo, unlike `game/`.

A stage with no `.stage` file draws exactly as it does today. That is the acceptance test the
whole feature is built around — the recorded background comparison stays byte-identical — and it is why
absence is the default rather than an empty file being required.

## Where the files live

```
stages/<name>.stage        the scene: what solids a stage has and where
stages/models/<name>.obj   the geometry, in any modeller that writes Wavefront OBJ
```

`<name>` is the stage's own name from its `bg.dat` — `The_Great_Wall`, `CUHK`,
`Brokeback_Clif`. Matching on the name the game already carries means no table to keep in step
and no index that shifts when a stage is added.

## The four axes, and why a model file only carries three

LF2 has **four independent position numbers**, not three, because the game never unified them:

| | what it is | where it comes from |
|---|---|---|
| `x` | across the stage, in the game's own pixels | authored |
| `jump` | LF2's vertical axis; subtracts from the row | authored |
| `row` | the floor row this point stands on — the game's `z` | authored |
| `depth` | parallax depth; 1.0 is the plane the fighters stand in | **per solid**, not per vertex |

`row` and `depth` are both "depth" in ordinary language and are *not* the same thing here. A
fighter's `z` is used directly as a screen row, so the depth axis projects down the screen at
slope 1 (claim C018) — but every object shifts by the camera **flat** whatever its `z`, while a
background layer shifts by `camera/depth` (claim C031). Those are two different cameras glued
together, and no perspective projection reproduces both.

**So `depth` is a property of the whole solid.** A pillar standing in the stage is at one
parallax depth; its vertices differ in `x`, `jump` and `row`. That is what makes an ordinary
OBJ file enough: its `v x y z` become `x`, `jump`, `row`, and the solid's `depth:` line
supplies the fourth.

A solid that genuinely spans parallax depths — a wall receding from the fighters' plane into
the distance — has to be split into several solids. That is a real limitation of LF2's own
projection, not of this format, and it is why the limitation is written down here rather than
worked around.

## The scene file

```
stage: The_Great_Wall

solid:
  model: models/gw_pillar.obj
  depth: layer hill1.bmp        # or a number, e.g.  depth: 3.92
  at: 1200 0 400                # x, jump, row -- added to every vertex
  tint: 255 255 255             # optional, multiplies the model's own colours
solid_end
```

### `depth: layer <file>`

Prefer this to a number. It names a layer of the stage's own `bg.dat` and takes that layer's
derived depth, so an authored solid sits **exactly** in the plane of the art it belongs with —
and if the game's data ever says otherwise, the solid moves with it rather than drifting away
from a hardcoded constant.

The depths are derivable because a layer's scroll rate is a perspective divide written as a
ratio: `depth = (stage_width - 794) / (span - 794)`, claim C031. `tools/re/stage_gaps.py
--depth` prints them per stage, which is where an author reads the number or the layer name to
use.

Two layers have **no** derivable depth and the loader refuses rather than guessing: a stage
whose width is 794 or less never pans (HK Coliseum), and a layer whose `span` is 794 or less
never moves. Both mean "infinitely far", which is a legitimate place for a backdrop and a
useless one for a solid.

### `at: <x> <jump> <row>`

Added to every vertex of the model, so one model can be placed several times. `row` is a screen
row in the game's own 550, and the stage's walkable band is its `zboundary:` — Brokeback Clif's
is 300..510, so a pillar standing on the floor at the near edge has `row` near 300.

## What is NOT in this format, deliberately

- **No depth for the existing layers.** They are derived (C031). Authoring them would be
  transcribing a number the data already gives, and transcribed numbers go stale.
- **No camera.** The projection is the game's and is not a choice — see
  `geom_stage_project` in `runtime/overrides/geom.h`.
- **No light.** The set is lit by the same key light the fighters are, from the same vector, so
  a solid's shading and a fighter's shadow cannot disagree. A per-stage light would be the
  first step to a set that looks lit from somewhere the shadows do not come from.
- **No materials beyond a tint.** The model carries its own vertex colours; a material system
  is what to add when a stage asks for one. Adding it first is the mistake issue #30 records,
  where five effects shipped together and read as a filter.

## The OBJ subset that is read

`v`, `vt`, `vn`, `f` (triangles and quads, which are split), and `#` comments. Everything else
is **skipped with a count**, and the loader reports how many lines it skipped — a parser that
silently ignores what it cannot match is how a broken model becomes a clean bill of health.

Faces may be `f a b c`, `f a/b c/d e/f` or `f a//b c//d e//f`. Negative (relative) indices are
supported. A face referring to a vertex that does not exist is an error, not a skip.
