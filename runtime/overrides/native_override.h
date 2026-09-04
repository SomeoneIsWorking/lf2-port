#ifndef LF2_NATIVE_OVERRIDE_H
#define LF2_NATIVE_OVERRIDE_H

#include <stdint.h>

typedef void (*Lf2NativeOverride)(void);

/* Runtime addresses are the authority; function names are diagnostic labels only. */
Lf2NativeOverride lf2_native_override_find(uint32_t guest_address, uint32_t excluded_address);

#endif
