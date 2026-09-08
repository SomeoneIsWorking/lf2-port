#ifndef LF2_GUEST_HEAP_H
#define LF2_GUEST_HEAP_H
#include <stdint.h>
uint32_t guest_alloc(uint32_t bytes);
uint32_t guest_calloc(uint32_t count, uint32_t size);
uint32_t guest_heap_used(void);
#endif
