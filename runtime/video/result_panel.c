#include "result_panel.h"

#include "geom.h"

enum {
    RESULT_LEFT = 150,
    RESULT_RIGHT = 640,
    RESULT_WIDTH = RESULT_RIGHT - RESULT_LEFT,
    RESULT_HEADER_H = 61,
    RESULT_FOOTER_H = 32,
    RESULT_SCREEN_H = 550,
};

static long active_frame = -1;
static int panel_top;
static int panel_bottom;

static int full_surface(int dl, int dt, int dr, int db, int src_w, int src_h, int sl, int st, int sr, int sb,
                        int height)
{
    return dl == RESULT_LEFT && dr - dl == RESULT_WIDTH && db - dt == height && src_w == RESULT_WIDTH &&
           src_h == height && sl == 0 && st == 0 && sr == src_w && sb == src_h;
}

int result_panel_blit_offset(long frame, int view_w, int dl, int dt, int dr, int db, int src_w, int src_h, int sl,
                             int st, int sr, int sb)
{
    /* fn_0041bc90 starts the score display with the complete 490x61 Summary header at
     * x=150. Its y is computed from the live row count, so y is deliberately not guessed. */
    if (full_surface(dl, dt, dr, db, src_w, src_h, sl, st, sr, sb, RESULT_HEADER_H)) {
        active_frame = frame;
        panel_top = dt;
        panel_bottom = RESULT_SCREEN_H;
    }

    if (active_frame != frame) return 0;

    /* The complete 490x32 footer is the panel's last piece. Remembering its bottom prevents
     * the wide mode caption, drawn afterwards in the same frame, from inheriting the shift. */
    if (full_surface(dl, dt, dr, db, src_w, src_h, sl, st, sr, sb, RESULT_FOOTER_H)) panel_bottom = db;

    if (dl < RESULT_LEFT || dr > RESULT_RIGHT || dt < panel_top || db > panel_bottom) return 0;
    return geom_item_offset_x(view_w, RESULT_LEFT, RESULT_WIDTH);
}

int result_panel_text_offset(long frame, int view_w, int x, int y, int h)
{
    if (active_frame != frame) return 0;
    if (x < RESULT_LEFT || x >= RESULT_RIGHT || y < panel_top || y + h > panel_bottom) return 0;
    return geom_item_offset_x(view_w, RESULT_LEFT, RESULT_WIDTH);
}
