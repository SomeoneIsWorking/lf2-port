#include "native_override.h"

#include <stddef.h>

#define LF2_NATIVE_OVERRIDES(X)                                                                                        \
    X(0040ef70)                                                                                                        \
    X(004148a0)                                                                                                        \
    X(00414a30)                                                                                                        \
    X(00416e10)                                                                                                        \
    X(00416e60)                                                                                                        \
    X(00416eb0)                                                                                                        \
    X(00416f10)                                                                                                        \
    X(00416f60)                                                                                                        \
    X(00416fb0)                                                                                                        \
    X(00417090)                                                                                                        \
    X(00419a60)                                                                                                        \
    X(00419e40)                                                                                                        \
    X(0041a050)                                                                                                        \
    X(0041a250)                                                                                                        \
    X(0041a5a0)                                                                                                        \
    X(0041ae60)                                                                                                        \
    X(0041b130)                                                                                                        \
    X(00423940)                                                                                                        \
    X(00423b00)                                                                                                        \
    X(004246b0)                                                                                                        \
    X(0043c4a0)                                                                                                        \
    X(0043e940)                                                                                                        \
    X(0043f010)

#define DECLARE_OVERRIDE(address) void fn_##address(void);
LF2_NATIVE_OVERRIDES(DECLARE_OVERRIDE)
#undef DECLARE_OVERRIDE

typedef struct {
    uint32_t address;
    Lf2NativeOverride function;
} NativeOverrideEntry;

#define OVERRIDE_ENTRY(address) {0x##address##u, fn_##address},
static const NativeOverrideEntry OVERRIDES[] = {LF2_NATIVE_OVERRIDES(OVERRIDE_ENTRY)};
#undef OVERRIDE_ENTRY

Lf2NativeOverride lf2_native_override_find(uint32_t guest_address, uint32_t excluded_address)
{
    if (guest_address == excluded_address) return NULL;
    for (size_t index = 0; index < sizeof OVERRIDES / sizeof OVERRIDES[0]; ++index) {
        if (OVERRIDES[index].address == guest_address) return OVERRIDES[index].function;
    }
    return NULL;
}
