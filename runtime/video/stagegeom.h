/* Loading hand-woven stage geometry: `stages/<name>.stage` and the OBJ models it names.
 *
 * The format and the reasoning behind it are docs/stage-geometry.md. What matters here:
 *
 *   - a stage with no file loads NOTHING and that is not an error, because it is the state of
 *     every stage until one is authored. The report says so rather than printing a clean zero.
 *   - `depth` is per SOLID, not per vertex, because LF2's parallax depth and its floor row are
 *     independent axes (claims C018 and C031) and a solid sits at one parallax depth. That is
 *     what lets an ordinary OBJ file carry the geometry: its v x y z become x, jump and row.
 *   - `depth: layer <file>` takes the depth from the stage's OWN bg.dat layer rather than from
 *     a number, so an authored solid cannot drift away from the art it belongs with.
 *
 * This file does no I/O of the game's data: the caller resolves a layer name to a span and a
 * stage width, because that is runtime/overrides/background.c's business and it already has
 * the registry. Keeping it out means the loader is testable offline, which tests/test_stagegeom.c
 * is.
 */
#ifndef LF2_STAGEGEOM_H
#define LF2_STAGEGEOM_H

#include "mesh.h"

/* Resolve a layer NAME to its bg.dat `width:` (its span) and the stage's own width, both in
 * the game's pixels. Return 0 if the stage has no such layer -- the loader then refuses the
 * solid and says which name it could not find, rather than placing it at a default depth.
 *
 * A function pointer rather than a direct call so the loader has no dependency on the guest at
 * all, which is what makes it testable without booting the game. */
typedef int (*StageLayerLookup)(void *ctx, const char *layer, int *span, int *stage_width);

typedef struct {
    MeshVertex *v;
    int         n;              /* vertices, a multiple of 3 */
    int         solids;         /* how many solids contributed */
    int         skipped_lines;  /* OBJ lines this loader does not read -- reported, not hidden */
    char        error[512];     /* empty when ok; otherwise why, naming the file and line.
                                 * Big enough that no message can be truncated mid-word: a
                                 * refusal that says half of which key was wrong is a refusal
                                 * the author has to guess at. */
} StageGeom;

/* Load `dir`/`name`.stage. Returns 1 on success INCLUDING the no-file case, where g->n is 0 and
 * g->error is empty -- absence is not failure. Returns 0 only when a file exists and is wrong,
 * with g->error saying where.
 *
 * Free with stagegeom_free. */
int  stagegeom_load(const char *dir, const char *name,
                    StageLayerLookup lookup, void *ctx, StageGeom *g);
void stagegeom_free(StageGeom *g);

#endif
