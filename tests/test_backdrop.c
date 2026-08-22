#include "backdrop.h"
#include "backdrop_layout.h"

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
    eq("a semantic world backdrop gets a repeated native row",
       backdrop_bottom_row(1, 528, 198, 0, 128, 800, 198, 0, 0, 800, 70, &b), 1);
    eq("the extension starts below the authored picture", b.dt, 198);
    eq("the extension has one destination row", b.db - b.dt, 1);
    eq("the extension keeps the source width", b.dr - b.dl, 800);
    eq("only the final source row is sampled", b.st, 69);
    eq("the final row has one-pixel height", b.sb - b.st, 1);

    eq("the next row is another 1:1 repetition", backdrop_bottom_row(1, 528, 199, 0, 128, 800, 198, 0, 0, 800, 70, &b),
       1);
    eq("an ordinary layer is not extended", backdrop_bottom_row(0, 528, 251, 0, 147, 800, 251, 0, 0, 800, 104, &b), 0);
    eq("a backdrop already reaching the bottom needs nothing",
       backdrop_bottom_row(1, 528, 528, 0, 0, 800, 528, 0, 0, 800, 528, &b), 0);
    eq("an empty source is refused", backdrop_bottom_row(1, 528, 198, 0, 128, 800, 198, 0, 0, 0, 0, &b), 0);

    int translation = -1, flags = -1;
    eq("Lion Forest's far plane has authored wide placement",
       backdrop_plane_placement("Lion_Forest", 800, 0, 1600, &translation, &flags), 1);
    eq("the far plane keeps the authored world origin", translation, 0);
    eq("only the far plane continues right and below", flags, BACKDROP_MIRROR_RIGHT | BACKDROP_EXTEND_BOTTOM);
    eq("the left mountain piece is registered",
       backdrop_plane_placement("Lion_Forest", 1100, 0, 1600, &translation, &flags), 1);
    eq("the left mountain keeps the same world origin", translation, 0);
    eq("the left mountain is not coverage", flags, 0);
    eq("the adjacent mountain piece is registered",
       backdrop_plane_placement("Lion_Forest", 1100, 800, 1600, &translation, &flags), 1);
    eq("the adjacent piece keeps the same world origin", translation, 0);
    eq("the exact outer piece has an authored right continuation", flags, BACKDROP_MIRROR_RIGHT);
    eq("the right partial mountain is registered",
       backdrop_plane_placement("Lion_Forest", 1400, 1216, 1600, &translation, &flags), 1);
    eq("all three parallax spans share one translation", translation, 0);
    eq("the exact outer prop also has an authored right continuation", flags, BACKDROP_MIRROR_RIGHT);
    eq("an undeclared Lion Forest layer is unchanged",
       backdrop_plane_placement("Lion_Forest", 2900, 0, 3200, &translation, &flags), 0);
    eq("another stage does not inherit Lion Forest's layout",
       backdrop_plane_placement("CUHK", 1100, 0, 1600, &translation, &flags), 0);
    eq("native width preserves the game's placement",
       backdrop_plane_placement("Lion_Forest", 800, 0, 794, &translation, &flags), 0);

    translation = flags = -1;
    backdrop_plane_placement("Lion_Forest", 800, 0, 1600, &translation, &flags);
    const int far_l = translation, far_r = translation + 800;
    eq("the first reflected continuation fills the remaining view",
       backdrop_mirror_segment(flags, 1600, 0, far_l, 128, far_r, 198, 0, 0, 800, 70, &b), 1);
    eq("the continuation begins where the authored picture ends", b.dl, 800);
    eq("the continuation reaches the viewport edge", b.dr, 1600);
    eq("the full native source width is preserved", b.sr - b.sl, 800);
    eq("the full native destination width is preserved", b.dr - b.dl, 800);
    eq("the native source height is preserved", b.sb - b.st, 70);
    eq("the native destination height is preserved", b.db - b.dt, 70);
    eq("the first segment reflects X to share its edge texel", b.mirror_x, 1);
    eq("no second segment is needed at 1600",
       backdrop_mirror_segment(flags, 1600, 1, far_l, 128, far_r, 198, 0, 0, 800, 70, &b), 0);
    eq("a partial viewport clip preserves equal widths",
       backdrop_mirror_segment(flags, 1302, 0, far_l, 128, far_r, 198, 0, 0, 800, 70, &b), 1);
    eq("the clipped source width matches its destination", b.sr - b.sl, b.dr - b.dl);

    unsigned char covered[1600] = {0};
    for (int x = far_l; x < far_r; x++) covered[x] = 1;
    backdrop_mirror_segment(flags, 1600, 0, far_l, 128, far_r, 198, 0, 0, 800, 70, &b);
    for (int x = b.dl; x < b.dr; x++) covered[x] = 1;
    int holes = 0;
    for (int x = 0; x < 1600; x++) holes += covered[x] == 0;
    eq("opaque authored art and its reflected edge leave zero wide-view holes", holes, 0);

    eq("the bottom row uses the same native reflected segment",
       backdrop_mirror_segment(flags, 1600, 0, far_l, 198, far_r, 199, 0, 69, 800, 70, &b), 1);
    eq("the bottom continuation remains one source row", b.sb - b.st, 1);
    eq("the bottom continuation remains one destination row", b.db - b.dt, 1);

    backdrop_plane_placement("Lion_Forest", 1100, 800, 1600, &translation, &flags);
    eq("the 300-pixel outer mountain continues at x=1100",
       backdrop_mirror_segment(flags, 1600, 0, 800, 147, 1100, 251, 0, 0, 300, 104, &b), 1);
    eq("its joined segment keeps the native 300-pixel width", b.dr - b.dl, 300);
    eq("its source width stays 300 pixels", b.sr - b.sl, 300);
    eq("its native 104-pixel height is unchanged", b.db - b.dt, 104);
    backdrop_plane_placement("Lion_Forest", 1400, 1216, 1600, &translation, &flags);
    eq("the 184-pixel outer mountain continues at x=1400",
       backdrop_mirror_segment(flags, 1600, 0, 1216, 155, 1400, 242, 0, 0, 184, 87, &b), 1);
    eq("its clipped width equals the source width", b.dr - b.dl, b.sr - b.sl);
    eq("its native 87-pixel height is unchanged", b.db - b.dt, 87);

    printf("backdrop: %d checks\n", checks);
    return 0;
}
