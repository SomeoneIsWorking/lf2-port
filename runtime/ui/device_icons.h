/* Paint keyboard/gamepad device indicators into both LF2 presentation paths. */
#ifndef LF2_DEVICE_ICONS_H
#define LF2_DEVICE_ICONS_H

#include <stdint.h>

enum { DEVICE_ICON_SIZE = 18 };

/* The pre-fight overlay borrows the HUD panel signal while drawing its Stage/Difficulty
 * controls, but it is not a player HUD. Keep that screen policy beside the icon boundary so
 * it can be falsified without booting the game. */
static inline int device_icon_hud_visible(int hud_up, int overlay_up) { return hud_up && !overlay_up; }

/* dev follows the port's device numbering: 0 keyboard, 1..4 gamepads. */
int device_icon_paint(uint32_t dst_pixels, int dst_w, int dst_h, int dst_pitch, int x, int y, int dev);
int device_icon_record(uint32_t dst_pixels, int x, int y, int dev);
void device_icons_shutdown(void);

#endif
