#ifndef LF2_STAGE_BANNER_H
#define LF2_STAGE_BANNER_H

/* Centre the fixed-width stage-intro banner only inside a running match. The source sheet
 * and row also occur in the mode menu's selected-row draw, so geometry alone is not an
 * identity. */
int stage_banner_offset(int match_up, int view_w, int source_w, int source_h, int top);

#endif
