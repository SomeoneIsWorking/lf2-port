#ifndef RESULT_PANEL_H
#define RESULT_PANEL_H

/* The post-match Summary panel is drawn inside the still-wide world view, so it cannot use
 * the whole-screen centring rule. These functions recognise the panel's own first/last
 * pieces and return the fixed-screen offset only for draws inside that panel. */
int result_panel_blit_offset(long frame, int view_w, int dl, int dt, int dr, int db, int src_w, int src_h, int sl,
                             int st, int sr, int sb);
int result_panel_text_offset(long frame, int view_w, int x, int y, int h);

#endif
