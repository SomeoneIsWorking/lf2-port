#include "backdrop.h"

#include <stdio.h>
#include <stdlib.h>

static int checks;

static void eq(const char *what, int got, int want)
{
    checks++;
    if (got != want) {
        fprintf(stderr, "FAIL: %s: got %d, want %d\n", what, got, want);
        exit(1);
    }
}

int main(void)
{
    BackdropBlit b = {0};
    eq("a semantic world backdrop gets an extension",
       backdrop_bottom_extension(1, 528, 0, 128, 1571, 198, 0, 0, 800, 70, &b), 1);
    eq("the extension starts below the authored picture", b.dt, 198);
    eq("the extension stops above the caption strip", b.db, 528);
    eq("the extension keeps the widened horizontal placement", b.dr - b.dl, 1571);
    eq("only the final source row is sampled", b.st, 69);
    eq("the final row has one-pixel height", b.sb - b.st, 1);

    eq("an ordinary layer is not extended", backdrop_bottom_extension(0, 528, 0, 147, 800, 251, 0, 0, 800, 104, &b), 0);
    eq("a backdrop already reaching the bottom needs nothing",
       backdrop_bottom_extension(1, 528, 0, 0, 1571, 528, 0, 0, 800, 528, &b), 0);
    eq("an empty source is refused", backdrop_bottom_extension(1, 528, 0, 128, 1571, 198, 0, 0, 0, 0, &b), 0);

    int l = -1, r = -1;
    eq("a short non-looping span is scaled", backdrop_scale_span(1100, 1571, 0, 800, &l, &r), 1);
    eq("the span starts at the viewport edge", l, 0);
    eq("the first piece ends at its proportional boundary", r, 1142);
    eq("the adjacent piece shares that exact boundary", backdrop_scale_span(1100, 1571, 800, 1100, &l, &r), 1);
    eq("the adjacent piece starts without a gap", l, 1142);
    eq("the authored span ends at the viewport edge", r, 1571);
    eq("native and narrower views preserve authored placement", backdrop_scale_span(1100, 794, 0, 800, &l, &r), 0);
    eq("a rectangle outside its declared span is refused", backdrop_scale_span(1100, 1571, 800, 1200, &l, &r), 0);

    const BackdropLayerLayout lion[] = {
        {800, 0, 128, 0, 0},  {1100, 0, 147, 0, 0},    {1100, 800, 147, 0, 0},
        {1400, 0, 170, 0, 0}, {1400, 1216, 155, 0, 0}, {2900, 0, 199, 253, 0},
    };
    eq("the first painted backdrop is a coverage plane", backdrop_plane_span(lion, 6, 0, 1571, 365), 800);
    eq("a two-piece mountain plane is selected", backdrop_plane_span(lion, 6, 1, 1571, 365), 1100);
    eq("both pieces receive the same span", backdrop_plane_span(lion, 6, 2, 1571, 365), 1100);
    eq("a separated mountain pair remains one parallax plane", backdrop_plane_span(lion, 6, 4, 1571, 365), 1400);
    eq("a declared looping layer is not scaled", backdrop_plane_span(lion, 6, 5, 3200, 365), 0);

    const BackdropLayerLayout props[] = {
        {967, 0, 128, 0, 0},  {967, 800, 128, 0, 0}, {1179, 282, 268, 0, 0}, {1179, 872, 268, 0, 0},
        {1210, 0, 283, 0, 0}, {1170, 0, 362, 0, 0},  {1170, 797, 362, 0, 0},
    };
    eq("a complete sky plane is selected", backdrop_plane_span(props, 7, 1, 1571, 362), 967);
    eq("paired lamps that do not cover from the left stay native", backdrop_plane_span(props, 7, 2, 1571, 362), 0);
    eq("an isolated grass patch stays native", backdrop_plane_span(props, 7, 4, 1571, 362), 0);
    eq("floor pieces are outside the backdrop band", backdrop_plane_span(props, 7, 5, 1571, 362), 0);
    eq("a first layer at the walkable floor is not a backdrop plane", backdrop_plane_span(props + 5, 2, 0, 1571, 362),
       0);

    printf("backdrop: %d checks\n", checks);
    return 0;
}
