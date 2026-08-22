/* The native renderer's painter-order depth contract.
 *
 * LF2 has already ordered its display list. The GPU stores that ordinal as depth so later
 * draws are nearer, then reuses the completed depth buffer to decide which character pixels
 * are still visible after weapons and other solid objects have painted over them.
 */
#ifndef LF2_PAINTER_DEPTH_H
#define LF2_PAINTER_DEPTH_H

static inline float painter_depth(int ordinal, int count) { return 1.0f - (float)(ordinal + 1) / (float)(count + 1); }

#endif
