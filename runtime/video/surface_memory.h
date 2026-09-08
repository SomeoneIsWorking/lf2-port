#ifndef LF2_SURFACE_MEMORY_H
#define LF2_SURFACE_MEMORY_H
#include <stdint.h>
uint32_t vram_alloc(uint32_t bytes);
void vram_report(void);
#endif
