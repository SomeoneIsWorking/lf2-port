/* Calls from native code into guest functions and guest imports, with the stack the guest's own
 * caller would have built. The return address names the guest call site being replaced. */
#ifndef LF2_GUEST_CALLS_H
#define LF2_GUEST_CALLS_H

#include <stdint.h>

void push_arguments(const uint32_t *args, unsigned count);
uint32_t call_guest_cdecl(uint32_t guest_address, const uint32_t *args, unsigned count, uint32_t return_address);
uint32_t call_guest_no_args(uint32_t guest_address, uint32_t return_address);
uint32_t call_import(uint32_t iat, const uint32_t *args, unsigned count, int caller_cleans, uint32_t return_address);
/* The guest's timeGetTime. */
uint32_t guest_time_ms(void);

#endif
