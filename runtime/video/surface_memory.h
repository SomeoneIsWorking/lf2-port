#ifndef LF2_SURFACE_MEMORY_H
#define LF2_SURFACE_MEMORY_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t vram_alloc(uint32_t bytes);
/* Where the next surface goes, for a snapshot. */
uint32_t vram_next(void);
void vram_restore(uint32_t next);
void vram_report(void);
#ifdef __cplusplus
}
#endif
#endif
