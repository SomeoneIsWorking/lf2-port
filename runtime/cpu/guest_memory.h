#ifndef LF2_GUEST_MEMORY_H
#define LF2_GUEST_MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct X86pMem X86pMem;

extern uint8_t *g_mem;

void guest_memory_init(void);
void guest_memory_shutdown(void);
void guest_memory_map(uint32_t address, uint32_t size);
void guest_memory_before_write(uint32_t address, size_t size);
const X86pMem *guest_memory_view(void);
int guest_memory_ready(void);
uint8_t *guest_memory_resolve(uint32_t address, size_t size);
uint8_t *guest_memory_require(uint32_t address, size_t size);
const char *guest_string(uint32_t address);
int guest_memory_dump(FILE *file, uint32_t address, uint32_t size);

/* Desktop retains its existing contiguous address calculation. Browser
 * callers resolve the complete borrowed span in the shared sparse mapper. */
static inline uint8_t *guest_pointer(uint32_t address, size_t size)
{
#if defined(__EMSCRIPTEN__) || defined(LF2_TEST_SPARSE_MEMORY)
    return guest_memory_require(address, size);
#else
    if (size > UINT32_MAX - address) abort();
    return g_mem + address;
#endif
}

/* A borrowed writable span must announce mutation before the caller changes it. */
static inline uint8_t *guest_write_pointer(uint32_t address, size_t size)
{
#if defined(__EMSCRIPTEN__) || defined(LF2_TEST_SPARSE_MEMORY)
    uint8_t *host = guest_memory_require(address, size);
    guest_memory_before_write(address, size);
    return host;
#else
    if (size > UINT32_MAX - address) abort();
    return g_mem + address;
#endif
}

#endif
