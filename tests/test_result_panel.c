#include "result_panel.h"

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

static int blit(long frame, int view, int l, int t, int w, int h)
{ return result_panel_blit_offset(frame, view, l, t, l + w, t + h, w, h, 0, 0, w, h); }

int main(void)
{
    const int wide = (1571 - 490) / 2 - 150;

    eq("an unrelated 490x61 picture does not arm the panel", blit(10, 1571, 149, 196, 490, 61), 0);
    eq("the Summary header is centred", blit(10, 1571, 150, 196, 490, 61), wide);
    eq("the centred panel bounds have the composition midpoint", 150 + wide + 490 / 2, 1571 / 2);
    eq("a row inside the armed panel is centred", blit(10, 1571, 150, 257, 490, 45), wide);
    eq("score text inside the panel is centred", result_panel_text_offset(10, 1571, 271, 272, 16), wide);
    eq("native width is byte-identical", result_panel_text_offset(10, 794, 271, 272, 16), 0);
    eq("the footer is centred", blit(10, 1571, 150, 302, 490, 32), wide);
    eq("the mode caption below the footer is not moved", result_panel_text_offset(10, 1571, 1390, 531, 16), 0);
    eq("the panel signal expires at the frame boundary", result_panel_text_offset(11, 1571, 271, 272, 16), 0);

    /* The negative that catches a recogniser which keys only on the destination size: the
     * first cut would arm on a cropped source and move unrelated art. */
    eq("a cropped 490x61 destination is not a Summary header",
       result_panel_blit_offset(20, 1571, 150, 196, 640, 257, 800, 100, 10, 10, 500, 71), 0);

    printf("result panel: %d checks\n", checks);
    return 0;
}
