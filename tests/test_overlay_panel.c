#include "overlay_panel.h"

#include <stdint.h>
#include <stdio.h>

static int checks;
static int failures;

static void equal(const char *what, int got, int expected)
{
    checks++;
    if (got == expected) return;
    failures++;
    fprintf(stderr, "FAIL %-52s got %d want %d\n", what, got, expected);
}

int main(void)
{
    const uint32_t candidates[] = {
        0x0042cbf5u, 0x0042cc12u, 0x0042cc2fu, 0x0042cc4cu, 0x0042cc69u, 0x0042cc89u, 0x0042cd0eu, 0x0042cd2bu,
    };
    for (int stage = 0; stage <= 1; stage++) {
        for (int selected = 0; selected < GEOM_OVERLAY_ITEMS; selected++) {
            const uint32_t expected =
                stage ? (selected == 3 ? 0x0042cd2bu : 0x0042cd0eu) : OVERLAY_PANEL_SELECTED_RETURN[selected];
            for (unsigned candidate = 0; candidate < sizeof(candidates) / sizeof(candidates[0]); candidate++)
                equal("exactly one final producer is classified per mode/selection",
                      overlay_panel_final(candidates[candidate], selected, stage), candidates[candidate] == expected);

            const int final_row = stage ? 3 : selected;
            int panel_x = 0, panel_y = 0;
            overlay_panel_origin(OVERLAY_PANEL_ROW_X[final_row] + 138, GEOM_OV_ROW_Y[final_row], selected, stage,
                                 &panel_x, &panel_y);
            equal("final producer recovers centred panel x", panel_x, OVERLAY_PANEL_X + 138);
            equal("final producer recovers panel y", panel_y, OVERLAY_PANEL_Y);
        }
    }
    equal("neighbouring call is not classified by screen coordinates", overlay_panel_final(0x0042cd30u, 3, 1), 0);
    equal("invalid selection cannot index a producer table", overlay_panel_final(0x0042cbf5u, GEOM_OVERLAY_ITEMS, 0),
          0);

    for (int row = 0; row < GEOM_OVERLAY_ITEMS; row++) {
        int left = 0, top = 0, right = 0, bottom = 0;
        overlay_panel_row_bounds(row, &left, &top, &right, &bottom);
        equal("native row top is the input model's decompiled boundary", top + OVERLAY_PANEL_Y, GEOM_OV_ROW_Y[row]);
        equal("native row bottom is the input model's decompiled boundary", bottom + OVERLAY_PANEL_Y,
              GEOM_OV_ROW_Y[row + 1]);
        equal("native selected row keeps its decompiled x", left + OVERLAY_PANEL_X, OVERLAY_PANEL_ROW_X[row]);
        equal("selected row remains centred", left, OVERLAY_PANEL_W - right);
    }

    for (int row = 3; row <= 4; row++) {
        int left = 0, top = 0, right = 0, bottom = 0;
        overlay_panel_value_bounds(row, &left, &top, &right, &bottom);
        equal("value well begins at the dynamic TextOutA producer anchor", left + OVERLAY_PANEL_X, 174);
        equal("value label ends one logical pixel before its well", OVERLAY_PANEL_LABEL_RIGHT + 1, left);
        equal("value well keeps the panel's shared inner right margin", right, OVERLAY_PANEL_W - 8);
        equal("value well top derives from the input row boundary", top + OVERLAY_PANEL_Y,
              GEOM_OV_ROW_Y[row] + OVERLAY_PANEL_VALUE_Y_INSET);
        equal("value well bottom derives from the input row boundary", bottom + OVERLAY_PANEL_Y,
              GEOM_OV_ROW_Y[row + 1] - OVERLAY_PANEL_VALUE_Y_INSET);
    }
    int left = -1, top = -1, right = -1, bottom = -1;
    overlay_panel_value_bounds(2, &left, &top, &right, &bottom);
    equal("non-value row has no hand-authored well", left | top | right | bottom, 0);

    printf("overlay panel: %d checks, %d failure(s)\n", checks, failures);
    return failures != 0;
}
