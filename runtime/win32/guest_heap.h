#ifndef LF2_GUEST_HEAP_H
#define LF2_GUEST_HEAP_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t guest_alloc(uint32_t bytes);
uint32_t guest_calloc(uint32_t count, uint32_t size);
uint32_t guest_heap_used(void);
/* The heap's next address, for a snapshot. */
uint32_t guest_heap_next(void);
void guest_heap_restore(uint32_t next);

#ifdef __cplusplus
}
#endif

#endif
