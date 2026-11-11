#include "guest_calls.h"

#include "guest.h"
#include "jit_executor.h"

#include <stddef.h>

enum { IAT_TIME_GET_TIME = 0x00447250 };

void push_arguments(const uint32_t *args, unsigned count)
{
    while (count) PUSH32(args[--count]);
}

uint32_t call_guest_cdecl(uint32_t guest_address, const uint32_t *args, unsigned count, uint32_t return_address)
{
    push_arguments(args, count);
    PUSH32(return_address);
    lf2_jit_call(guest_address);
    R(ESP) += 4 * count;
    return R(EAX);
}

uint32_t call_import(uint32_t iat, const uint32_t *args, unsigned count, int caller_cleans, uint32_t return_address)
{
    push_arguments(args, count);
    PUSH32(return_address);
    dispatch(LD32(iat));
    if (caller_cleans) R(ESP) += 4 * count;
    return R(EAX);
}

uint32_t call_guest_no_args(uint32_t guest_address, uint32_t return_address)
{
    return call_guest_cdecl(guest_address, NULL, 0, return_address);
}

uint32_t guest_time_ms(void)
{
    return call_import(IAT_TIME_GET_TIME, NULL, 0, 0, 0x0043d162);
}
