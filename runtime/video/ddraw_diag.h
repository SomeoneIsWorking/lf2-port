#ifndef LF2_DDRAW_DIAG_H
#define LF2_DDRAW_DIAG_H

#include <stdint.h>

/* The DirectDraw boundary owns the frame diagnostics because they inspect both the
 * presented pixels and DirectDraw-specific counters. Presentation calls this one narrow
 * seam instead of reaching into each diagnostic independently. */
int ddraw_frame_pixels_wanted(long frame);
void ddraw_frame_diagnostics(const uint8_t *pixels, int w, int h, int pitch, long frame);

#endif
