#ifndef LF2_GDI_H
#define LF2_GDI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The size of the last bitmap LoadImage handed back, and how many it has handed back at all. */
int gdi_last_bitmap(int *w, int *h, long *loaded_total);
void gdi_last_bitmap_consume(void);

/* The handle tables the guest holds handles into, as a snapshot section. Loading truncates them
 * and frees the bitmaps loaded since; handles past what this process loaded name no bitmap. */
size_t gdi_state_size(void);
void gdi_state_save(void *out);
void gdi_state_load(const void *in);

#ifdef __cplusplus
}
#endif

#endif
