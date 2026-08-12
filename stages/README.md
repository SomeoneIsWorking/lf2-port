# Hand-woven stage geometry

Authored 3D sets for the game's stages. **`docs/stage-geometry.md` is the format** and the
reasoning behind every choice in it; this file is only where the files go.

```
stages/<name>.stage        the scene
stages/models/<name>.obj   the geometry, from any modeller that writes Wavefront OBJ
```

`<name>` is the stage's own name from its `bg.dat`, with the underscores it is spelled with
there — `The_Great_Wall`, `Brokeback_Clif`, `CUHK`. The port reads that name out of the
background record the game itself filled in (claim C033), so there is no table to keep in step.
`tools/re/stage_gaps.py --depth` prints each stage's layers with the depth every one of them
already implies, which is what an author picks a `depth: layer <file>` from.

None of this is game content. A `.stage` file and the models it names are the port's own work
and are committed here; nothing under `game/` ever is.

## This directory is empty on purpose

No stage has been woven yet, and an empty directory is the honest state — the port loads what
is here and a stage with no file draws exactly as it always has. It is not a placeholder to be
filled with something plausible: a solid at a guessed depth looks like geometry rather than
like a bug, which is why the loader refuses a depth it cannot derive instead of defaulting one.

CMake copies this directory next to the built binary after every build, and that is where the
running port looks first — its working directory is the *game* tree, which is not part of this
repo. `LF2_STAGE_GEOM=1` makes a stage with no geometry say so and name every directory it
looked in; `tools/e2e.sh stage_geom` asserts that end to end.
