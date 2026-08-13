/* The committed PvE stage pack must be loadable without booting the game (issue #62).
 *
 * This is deliberately separate from test_stagegeom's format fixtures. Those prove that a bad
 * input is refused; this is the shipping corpus and proves that every scene's `depth: layer`
 * names a real plane for ITS stage. A file that misses a layer name otherwise looks just like a
 * stage the author did not weave until a player reaches it.
 */
#include "video/stagegeom.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    int stage_width;
    const char *layer_a;
    int span_a;
    const char *layer_b;
    int span_b;
    int solids;
} Stage;

static const Stage stages[] = {
    { "Brokeback_Clif", 1500, "bc1.bmp", 1379, "bc4.bmp", 1500, 3 },
    { "CUHK", 1600, "floor1.bmp", 1170, "floor3.bmp", 1600, 3 },
    { "Forbidden_Tower", 2400, "w1.bmp", 1500, "g1.bmp", 2300, 3 },
    { "The_Great_Wall", 2400, "hill1.bmp", 1204, "road2.bmp", 2400, 3 },
    /* No observable layer parallax at this width; its stage file intentionally uses 1.0. */
    { "HK_Coliseum", 794, NULL, 0, NULL, 0, 3 },
    { "Lion_Forest", 3200, "forestm3.bmp", 1400, "land1.bmp", 2950, 3 },
    { "Queen's_Island", 2400, "qi2.bmp", 1200, "qi3.bmp", 2300, 3 },
    { "Stanley_Prison", 2400, "wall.bmp", 2400, NULL, 0, 3 },
    { "Tai_Hom_Village", 1600, "2al.bmp", 1255, "1dl.bmp", 1520, 3 },
    { "Template1", 1600, "pic3.bmp", 1000, "pic5.bmp", 1500, 2 },
    { "Template2", 1600, "pic3.bmp", 1000, "pic5.bmp", 1500, 2 },
    { "Template3", 1600, "pic3.bmp", 1000, "pic5.bmp", 1500, 2 },
};

static int lookup(void *ctx, const char *name, int *span, int *stage_width)
{
    const Stage *s = ctx;
    *stage_width = s->stage_width;
    if (s->layer_a && strcmp(name, s->layer_a) == 0) { *span = s->span_a; return 1; }
    if (s->layer_b && strcmp(name, s->layer_b) == 0) { *span = s->span_b; return 1; }
    return 0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "../stages";
    int failures = 0, checked = 0;
    for (unsigned i = 0; i < sizeof stages / sizeof stages[0]; i++) {
        StageGeom g;
        const Stage *s = &stages[i];
        const int r = stagegeom_load(dir, s->name, lookup, (void *)s, &g);
        checked++;
        if (!r || g.solids != s->solids || g.n == 0) {
            printf("  FAIL  %s: %s%s%d solid(s), %d vertex/vertices\n", s->name,
                   r ? "loaded " : "REFUSED: ", r ? "" : g.error,
                   r ? g.solids : 0, r ? g.n : 0);
            failures++;
        }
        /* A stage at 794 cannot derive a layer plane; its explicit 1.0 is meaningful only if
         * it reaches vertices, not if the parser quietly omitted its solid. */
        if (r && strcmp(s->name, "HK_Coliseum") == 0 && g.n && g.v[0].depth != 1.0f) {
            printf("  FAIL  HK_Coliseum: explicit fighters-plane depth became %.4f\n",
                   (double)g.v[0].depth);
            failures++;
        }
        stagegeom_free(&g);
    }
    printf("stage assets: %d scene(s) checked, %d failure(s)\n", checked, failures);
    if (!checked) {
        printf("  FAIL  no authored stages were checked\n");
        return 1;
    }
    return failures ? 1 : 0;
}
