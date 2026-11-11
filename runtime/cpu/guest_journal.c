#include "guest_journal.h"

#include "guest_memory.h"
#include "jit_executor.h"
#include "lf2_log.h"

#include <stdlib.h>

#if defined(__EMSCRIPTEN__) || defined(LF2_TEST_SPARSE_MEMORY) || defined(_WIN32)

int guest_journal_start(GuestSpanFilter tracked)
{
    (void)tracked;
    lf2_log_writef(LF2_LOG_WARNING, "guest-journal", "this platform has no guest write journal");
    return 0;
}
void guest_journal_stop(void)
{
}
int guest_journal_active(void)
{
    return 0;
}
uint32_t guest_journal_page_size(void)
{
    return 0;
}
void guest_journal_for_each_open_page(GuestPageVisitor visit, void *context)
{
    (void)visit;
    (void)context;
}
uint64_t guest_journal_mark(void)
{
    abort();
}
void guest_journal_rewind(uint64_t mark, GuestPageVisitor restored, void *context)
{
    (void)mark;
    (void)restored;
    (void)context;
    abort();
}
void guest_journal_release(uint64_t mark)
{
    (void)mark;
}
void guest_journal_note_write(uint32_t address, size_t size)
{
    (void)address;
    (void)size;
}
void guest_journal_track(uint32_t address, uint32_t size)
{
    (void)address;
    (void)size;
}

#else

#include <errno.h>
#include <signal.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

#define ADDRESS_SPACE UINT64_C(0x100000000)
/* Pre-images kept at once: the rollback window's writes, VRAM included, with room to spare. */
#define CAPACITY_BYTES (UINT64_C(256) << 20)

enum { PAGE_UNTRACKED = 0, PAGE_CLEAN = 1, PAGE_RECORDING = 2, PAGE_WRITTEN = 3 };

static int active;
static GuestSpanFilter filter;
static uint32_t page_size;
static uint32_t page_shift;
static uint64_t capacity;
static _Atomic uint8_t *page_state;
static uint8_t *images;
static uint32_t *entry_page;
/* Entries [head, tail) hold pre-images; [open, tail) are the open interval's. */
static _Atomic uint64_t tail;
static uint64_t head;
static uint64_t open_from;
static struct sigaction previous_segv;
static struct sigaction previous_bus;

static void fatal(const char *message)
{
    const ssize_t ignored = write(STDERR_FILENO, message, strlen(message));
    (void)ignored;
    abort();
}

static uint8_t *page_host(uint32_t page)
{
    return g_mem + ((uint64_t)page << page_shift);
}

/* Async-signal-safe: the fault handler protects pages too. */
static void protect(uint32_t page, int prot)
{
    if (mprotect(page_host(page), page_size, prot) == 0) return;
    char message[] = "guest-journal: mprotect failed, errno 000\n";
    const int error = errno;
    message[sizeof message - 3] = (char)('0' + error % 10);
    message[sizeof message - 4] = (char)('0' + error / 10 % 10);
    message[sizeof message - 5] = (char)('0' + error / 100 % 10);
    fatal(message);
}

/* Keeps a clean page's pre-image and makes it writable. 0 for a page the journal does not track. */
static int record(uint32_t page)
{
    for (;;) {
        uint8_t state = PAGE_CLEAN;
        if (atomic_compare_exchange_strong(&page_state[page], &state, PAGE_RECORDING)) break;
        if (state == PAGE_UNTRACKED) return 0;
        if (state == PAGE_WRITTEN) return 1;
    }
    const uint64_t sequence = atomic_fetch_add(&tail, 1);
    if (sequence - head >= capacity) fatal("guest-journal: more pages written than the journal holds\n");
    const uint64_t slot = sequence % capacity;
    memcpy(images + (slot << page_shift), page_host(page), page_size);
    entry_page[slot] = page;
    protect(page, PROT_READ | PROT_WRITE);
    atomic_store(&page_state[page], PAGE_WRITTEN);
    return 1;
}

static void chain(const struct sigaction *previous, int signal, siginfo_t *info, void *context)
{
    if (previous->sa_flags & SA_SIGINFO) {
        previous->sa_sigaction(signal, info, context);
        return;
    }
    if (previous->sa_handler == SIG_IGN || previous->sa_handler == SIG_DFL) {
        /* The faulting write runs again under the default action and is reported as itself. */
        sigaction(signal, previous, NULL);
        return;
    }
    previous->sa_handler(signal);
}

static void on_fault(int signal, siginfo_t *info, void *context)
{
    const uintptr_t address = (uintptr_t)info->si_addr;
    const uintptr_t base = (uintptr_t)g_mem;
    if (address >= base && address - base < ADDRESS_SPACE && record((uint32_t)((address - base) >> page_shift))) return;
    chain(signal == SIGBUS ? &previous_bus : &previous_segv, signal, info, context);
}

static void *reserve(uint64_t bytes)
{
    void *host = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return host == MAP_FAILED ? NULL : host;
}

static void track_span(uint32_t address, uint32_t size, const uint8_t *host, void *context)
{
    (void)host;
    (void)context;
    guest_journal_track(address, size);
}

int guest_journal_start(GuestSpanFilter tracked)
{
    if (active) return 1;
    if (!guest_memory_ready() || !g_mem) {
        lf2_log_writef(LF2_LOG_WARNING, "guest-journal", "guest memory is not one contiguous reservation");
        return 0;
    }
    const long size = sysconf(_SC_PAGESIZE);
    if (size <= 0 || (size & (size - 1)) != 0) {
        lf2_log_writef(LF2_LOG_WARNING, "guest-journal", "unusable host page size %ld", size);
        return 0;
    }
    page_size = (uint32_t)size;
    page_shift = (uint32_t)__builtin_ctz(page_size);
    capacity = CAPACITY_BYTES >> page_shift;
    if (!page_state) page_state = reserve(ADDRESS_SPACE >> page_shift);
    if (!images) images = reserve(CAPACITY_BYTES);
    if (!entry_page) entry_page = reserve(capacity * sizeof *entry_page);
    if (!page_state || !images || !entry_page) {
        lf2_log_writef(LF2_LOG_WARNING, "guest-journal", "cannot reserve the journal's %llu MB",
                       (unsigned long long)(CAPACITY_BYTES >> 20));
        return 0;
    }
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_sigaction = on_fault;
    action.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGSEGV, &action, &previous_segv) != 0 || sigaction(SIGBUS, &action, &previous_bus) != 0) {
        lf2_log_writef(LF2_LOG_WARNING, "guest-journal", "cannot install the write fault handler");
        return 0;
    }
    filter = tracked;
    active = 1;
    guest_memory_for_each_span(track_span, NULL);
    lf2_log_writef(LF2_LOG_INFO, "guest-journal", "tracking guest writes in %u-byte pages", page_size);
    return 1;
}

void guest_journal_stop(void)
{
    if (!active) return;
    active = 0;
    for (uint64_t page = 0; page < (ADDRESS_SPACE >> page_shift); page++) {
        if (atomic_load(&page_state[page]) == PAGE_UNTRACKED) continue;
        protect((uint32_t)page, PROT_READ | PROT_WRITE);
        atomic_store(&page_state[page], PAGE_UNTRACKED);
    }
    sigaction(SIGSEGV, &previous_segv, NULL);
    sigaction(SIGBUS, &previous_bus, NULL);
    head = open_from = 0;
    atomic_store(&tail, 0);
}

int guest_journal_active(void)
{
    return active;
}

uint32_t guest_journal_page_size(void)
{
    return page_size;
}

void guest_journal_track(uint32_t address, uint32_t size)
{
    if (!active || !size || !filter(address)) return;
    const uint32_t first = address >> page_shift;
    const uint32_t last = (uint32_t)(((uint64_t)address + size - 1) >> page_shift);
    /* A page an earlier span shares is already tracked, and may hold this interval's pre-image. */
    for (uint64_t page = first; page <= last; page++) {
        if (atomic_load(&page_state[page]) != PAGE_UNTRACKED) continue;
        protect((uint32_t)page, PROT_READ);
        atomic_store(&page_state[page], PAGE_CLEAN);
    }
}

void guest_journal_note_write(uint32_t address, size_t size)
{
    if (!active || !size) return;
    const uint32_t first = address >> page_shift;
    const uint32_t last = (uint32_t)(((uint64_t)address + size - 1) >> page_shift);
    for (uint64_t page = first; page <= last; page++) record((uint32_t)page);
}

void guest_journal_for_each_open_page(GuestPageVisitor visit, void *context)
{
    const uint64_t end = atomic_load(&tail);
    for (uint64_t sequence = open_from; sequence < end; sequence++)
        visit(entry_page[sequence % capacity] << page_shift, page_size, context);
}

uint64_t guest_journal_mark(void)
{
    const uint64_t end = atomic_load(&tail);
    for (uint64_t sequence = open_from; sequence < end; sequence++) {
        const uint32_t page = entry_page[sequence % capacity];
        protect(page, PROT_READ);
        atomic_store(&page_state[page], PAGE_CLEAN);
    }
    open_from = end;
    return end;
}

void guest_journal_rewind(uint64_t mark, GuestPageVisitor restored, void *context)
{
    const uint64_t end = atomic_load(&tail);
    if (mark < head || mark > end) fatal("guest-journal: rewind to a mark the journal no longer holds\n");
    for (uint64_t sequence = end; sequence-- > mark;) {
        const uint64_t slot = sequence % capacity;
        const uint32_t page = entry_page[slot];
        if (atomic_load(&page_state[page]) == PAGE_CLEAN) {
            protect(page, PROT_READ | PROT_WRITE);
            atomic_store(&page_state[page], PAGE_WRITTEN);
        }
        memcpy(page_host(page), images + (slot << page_shift), page_size);
    }
    for (uint64_t sequence = mark; sequence < end; sequence++) {
        const uint32_t page = entry_page[sequence % capacity];
        if (atomic_load(&page_state[page]) != PAGE_WRITTEN) continue;
        protect(page, PROT_READ);
        atomic_store(&page_state[page], PAGE_CLEAN);
        const uint32_t address = page << page_shift;
        lf2_jit_invalidate(address, page_size);
        if (restored) restored(address, page_size, context);
    }
    atomic_store(&tail, mark);
    open_from = mark;
}

void guest_journal_release(uint64_t mark)
{
    if (mark > head && mark <= atomic_load(&tail)) head = mark;
}

#endif
