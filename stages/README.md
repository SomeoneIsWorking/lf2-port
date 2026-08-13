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

## The PvE set pack

Every shipped Stage Mode background now has a small, original low-poly set: distant silhouettes
plus a nearer landmark at the fighters' plane. They are deliberately additive. The game's painted
layers remain the stage; these solids give HD2D real surfaces, normals and depth to light without
copying any game art.

The stage files name the planes in each background's own parallax data (`depth: layer …`) where
that plane exists. `HK_Coliseum` is the explicit exception: its stage is 794 pixels wide and never pans, so
no layer's depth is observable. Its new arena solids use the documented fighters' plane (`1.0`),
not a fabricated layer-derived value.

CMake copies this directory next to the built binary after every build, and that is where the
running port looks first — its working directory is the *game* tree, which is not part of this
repo. `LF2_STAGE_GEOM=1` makes a stage with no geometry say so and name every directory it
looked in; `tools/e2e.sh stage_geom` asserts that end to end.
