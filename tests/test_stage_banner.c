#include "stage_banner.h"

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
    eq("stage intro is centred in a match", stage_banner_offset(1, 978, 794, 600, 299), 92);
    eq("native width is unchanged", stage_banner_offset(1, 794, 794, 600, 299), 0);
    eq("mode-menu Demo highlight is not a stage banner", stage_banner_offset(0, 978, 794, 600, 339), 0);
    eq("wrong source width is not a banner", stage_banner_offset(1, 978, 793, 600, 299), 0);
    eq("wrong source height is not a banner", stage_banner_offset(1, 978, 794, 599, 299), 0);
    eq("draw above the banner band is not a banner", stage_banner_offset(1, 978, 794, 600, 293), 0);
    eq("draw below the banner band is not a banner", stage_banner_offset(1, 978, 794, 600, 342), 0);

    printf("stage banner: %d checks\n", checks);
    return 0;
}
