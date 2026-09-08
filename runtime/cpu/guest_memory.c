#include "guest_memory.h"
#include "cpu.h"
#include "memory_sparse.h"
#include "jit_executor.h"
#include "lf2_log.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#if !defined(__EMSCRIPTEN__) && !defined(LF2_TEST_SPARSE_MEMORY)
#include <sys/mman.h>
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif
#endif

typedef struct GuestAllocation {
    struct GuestAllocation *next;
    uint8_t *host;
    uint32_t address;
    uint32_t size;
} GuestAllocation;

uint8_t *g_mem;
static X86pMem memory;
static GuestAllocation *allocations;
static int initialized;
static pthread_mutex_t mapping_lock = PTHREAD_MUTEX_INITIALIZER;

static void memory_failure(const char *operation, uint32_t address, size_t size)
{
    lf2_log_writef(LF2_LOG_ERROR, "guest-memory", "%s: guest=%08x bytes=%zu", operation, address, size);
    abort();
}

void guest_memory_init(void)
{
    if (initialized) memory_failure("memory already initialized", 0, 0);
#if defined(__EMSCRIPTEN__) || defined(LF2_TEST_SPARSE_MEMORY)
    memory.sparse = x86p_sparse_create();
    if (!memory.sparse) memory_failure("sparse address map allocation failed", 0, 0);
#else
    const size_t address_space = UINT64_C(0x100000000);
    void *host = mmap(NULL, address_space, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (host == MAP_FAILED) memory_failure("contiguous address reservation failed", 0, address_space);
    g_mem = host;
    memory.host = g_mem;
    memory.size = UINT32_MAX;
#endif
    initialized = 1;
    if (atexit(guest_memory_shutdown) != 0) memory_failure("cannot register memory teardown", 0, 0);
}

void guest_memory_shutdown(void)
{
    if (!initialized) return;
    x86p_sparse_destroy(memory.sparse);
    while (allocations) {
        GuestAllocation *next = allocations->next;
        free(allocations->host);
        free(allocations);
        allocations = next;
    }
#if !defined(__EMSCRIPTEN__) && !defined(LF2_TEST_SPARSE_MEMORY)
    if (munmap(g_mem, UINT64_C(0x100000000)) != 0) memory_failure("address release failed", 0, 0);
#endif
    memory = (X86pMem){0};
    g_mem = NULL;
    initialized = 0;
}

void guest_memory_map(uint32_t address, uint32_t size)
{
    if (!initialized || !size || (uint64_t)address + size > UINT64_C(0x100000000))
        memory_failure("invalid allocation span", address, size);
    if (!memory.sparse) return;
    GuestAllocation *allocation = calloc(1, sizeof *allocation);
    uint8_t *host = calloc(size, 1);
    if (!allocation || !host) {
        free(allocation);
        free(host);
        memory_failure("host allocation failed", address, size);
    }
    lf2_jit_invalidate(address, size);
    pthread_mutex_lock(&mapping_lock);
    const int mapped = x86p_sparse_map(memory.sparse, address, host, size);
    pthread_mutex_unlock(&mapping_lock);
    if (!mapped) {
        free(allocation);
        free(host);
        memory_failure("overlapping or invalid sparse allocation", address, size);
    }
    *allocation = (GuestAllocation){.next = allocations, .host = host, .address = address, .size = size};
    allocations = allocation;
}

const X86pMem *guest_memory_view(void)
{
    return &memory;
}

int guest_memory_ready(void)
{
    return initialized;
}

uint8_t *guest_memory_resolve(uint32_t address, size_t size)
{
    uint8_t *host = NULL;
    if (size > UINT32_MAX) return NULL;
    pthread_mutex_lock(&mapping_lock);
    const int resolved = x86p_mem_resolve(&memory, address, size ? (uint32_t)size : 1, &host);
    pthread_mutex_unlock(&mapping_lock);
    return resolved ? host : NULL;
}

uint8_t *guest_memory_require(uint32_t address, size_t size)
{
    uint8_t *host = guest_memory_resolve(address, size);
    if (!host) memory_failure("unmapped native access", address, size);
    return host;
}

const char *guest_string(uint32_t address)
{
    uint8_t *host;
    const uint32_t limit = UINT32_MAX - address;
    pthread_mutex_lock(&mapping_lock);
    const uint32_t available = memory.sparse ? x86p_sparse_span(memory.sparse, address, limit, &host) : limit;
    pthread_mutex_unlock(&mapping_lock);
    if (!memory.sparse) host = g_mem + address;
    if (!available || !memchr(host, 0, available)) memory_failure("unterminated guest string", address, available);
    return (const char *)host;
}

int guest_memory_dump(FILE *file, uint32_t address, uint32_t size)
{
    if (!initialized || (uint64_t)address + size > UINT64_C(0x100000000)) return 0;
    while (size) {
        uint8_t *host = g_mem ? g_mem + address : NULL;
        pthread_mutex_lock(&mapping_lock);
        uint32_t span = memory.sparse ? x86p_sparse_span(memory.sparse, address, size, &host) : size;
        pthread_mutex_unlock(&mapping_lock);
        if (!span || !host || fwrite(host, 1, span, file) != span) return 0;
        address += span;
        size -= span;
    }
    return 1;
}

void guest_memory_before_write(uint32_t address, size_t size)
{
    if (size > UINT32_MAX || (uint64_t)address + size > UINT64_C(0x100000000))
        memory_failure("invalid write span", address, size);
    lf2_jit_invalidate(address, (uint32_t)size);
}
