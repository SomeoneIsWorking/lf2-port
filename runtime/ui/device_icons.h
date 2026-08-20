/* Paint keyboard/gamepad device indicators into both LF2 presentation paths. */
#ifndef LF2_DEVICE_ICONS_H
#define LF2_DEVICE_ICONS_H

#include <stdint.h>

enum { DEVICE_ICON_SIZE = 18 };

/* dev follows the port's device numbering: 0 keyboard, 1..4 gamepads. */
int device_icon_paint(uint32_t dst_pixels, int dst_w, int dst_h, int dst_pitch,
                      int x, int y, int dev);
int device_icon_record(uint32_t dst_pixels, int x, int y, int dev);
void device_icons_shutdown(void);

#endif
