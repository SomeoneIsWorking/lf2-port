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

    printf("backdrop: %d checks\n", checks);
    return 0;
}
