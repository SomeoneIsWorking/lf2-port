#ifndef LF2_OVERLAY_PANEL_H
#define LF2_OVERLAY_PANEL_H

#include <stdint.h>

#include "geom.h"

/* FUN_00429730 owns the pre-fight overlay. These return addresses are its CHARMENU
 * producers, read from the LF2 2.0a branch rather than inferred from rectangles. The port
 * appends its complete native panel after the branch's final static draw, before the game's
 * dynamic TextOutA values. Every original draw remains underneath as the allocation fallback. */
static const uint32_t OVERLAY_PANEL_SELECTED_RETURN[GEOM_OVERLAY_ITEMS] = {
    0x0042cbf5u, 0x0042cc12u, 0x0042cc2fu, 0x0042cc4cu, 0x0042cc69u, 0x0042cc89u,
};

enum OverlayPanelAction {
    OVERLAY_PANEL_APPLY_NONE = 0,
    OVERLAY_PANEL_APPENDED,
};

static inline int overlay_panel_final(uint32_t return_address, int selected, int stage_mode)
{
    if (selected < 0 || selected >= GEOM_OVERLAY_ITEMS) return 0;
    if (!stage_mode) return return_address == OVERLAY_PANEL_SELECTED_RETURN[selected];
    return return_address == (selected == 3 ? 0x0042cd2bu : 0x0042cd0eu);
}

enum {
    OVERLAY_PANEL_X = GEOM_OV_X0,
    OVERLAY_PANEL_Y = 3,
    OVERLAY_PANEL_W = GEOM_OV_X1 - GEOM_OV_X0,
    OVERLAY_PANEL_H = 166,
    OVERLAY_PANEL_VALUE_X = 174 - OVERLAY_PANEL_X,
    /* Labels end one logical pixel before the dynamic-value well begins. */
    OVERLAY_PANEL_LABEL_RIGHT = OVERLAY_PANEL_VALUE_X - 1,
    OVERLAY_PANEL_VALUE_RIGHT = OVERLAY_PANEL_W - 8,
    OVERLAY_PANEL_VALUE_Y_INSET = 3,
};

/* The selected variants begin at these exact x coordinates in FUN_00429730.  Their y
 * coordinates are GEOM_OV_ROW_Y, shared with the shipping input hit test. */
static const int OVERLAY_PANEL_ROW_X[GEOM_OVERLAY_ITEMS] = {92, 64, 40, 15, 37, 101};

static inline void overlay_panel_row_bounds(int row, int *left, int *top, int *right, int *bottom)
{
    if (row < 0 || row >= GEOM_OVERLAY_ITEMS) {
        *left = *top = *right = *bottom = 0;
        return;
    }
    *left = OVERLAY_PANEL_ROW_X[row] - OVERLAY_PANEL_X;
    *right = OVERLAY_PANEL_W - *left;
    *top = GEOM_OV_ROW_Y[row] - OVERLAY_PANEL_Y;
    *bottom = GEOM_OV_ROW_Y[row + 1] - OVERLAY_PANEL_Y;
}

static inline void overlay_panel_origin(int final_x, int final_y, int selected, int stage_mode, int *panel_x,
                                        int *panel_y)
{
    const int final_row = stage_mode ? 3 : selected;
    *panel_x = final_x - OVERLAY_PANEL_ROW_X[final_row] + OVERLAY_PANEL_X;
    *panel_y = final_y - GEOM_OV_ROW_Y[final_row] + OVERLAY_PANEL_Y;
}

static inline void overlay_panel_value_bounds(int row, int *left, int *top, int *right, int *bottom)
{
    if (row != 3 && row != 4) {
        *left = *top = *right = *bottom = 0;
        return;
    }
    *left = OVERLAY_PANEL_VALUE_X;
    *right = OVERLAY_PANEL_VALUE_RIGHT;
    *top = GEOM_OV_ROW_Y[row] - OVERLAY_PANEL_Y + OVERLAY_PANEL_VALUE_Y_INSET;
    *bottom = GEOM_OV_ROW_Y[row + 1] - OVERLAY_PANEL_Y - OVERLAY_PANEL_VALUE_Y_INSET;
}

/* The override identifies the final producer before the shared draw helper loses that
 * identity; DirectDraw first draws it normally, then supplies its destination to apply.
 * APPENDED means logical pixels changed and their surface caches must be invalidated. */
void overlay_panel_hint_set(int selected, int stage_mode);
void overlay_panel_hint_clear(void);
enum OverlayPanelAction overlay_panel_apply(uint32_t dst_pixels, int dst_w, int dst_h, int dst_pitch, int x, int y,
                                            float output_scale);
void overlay_panel_report(void);
void overlay_panel_shutdown(void);

#endif
