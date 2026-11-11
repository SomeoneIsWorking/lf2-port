#include "guest_journal.h"
#include "guest_memory.h"

#include <stdio.h>
#include <string.h>

static unsigned invalidated;
void lf2_jit_invalidate(uint32_t address, uint32_t length)
{
    (void)address;
    (void)length;
    ++invalidated;
}

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            printf("FAIL line %d: %s\n", __LINE__, #condition);                                                        \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

static int track_all(uint32_t address)
{
    (void)address;
    return 1;
}

static unsigned spans;
static void count_span(uint32_t address, uint32_t size, const uint8_t *host, void *context)
{
    (void)address;
    (void)size;
    (void)host;
    (void)context;
    ++spans;
}

static unsigned visited;
static void count_page(uint32_t address, uint32_t size, void *context)
{
    (void)address;
    (void)size;
    (void)context;
    ++visited;
}

int main(void)
{
    enum { BASE = 0x20000000u, LATE = 0x30000000u };
    guest_memory_init();
    guest_memory_map(BASE, 0x10000);
    uint8_t *bytes = g_mem + BASE;
    bytes[0] = 1;
    CHECK(guest_journal_start(track_all));
    const uint32_t page = guest_journal_page_size();
    CHECK(page >= 4096);

    const uint64_t start = guest_journal_mark();
    bytes[0] = 2;        /* a plain store faults into the journal */
    bytes[page + 1] = 3; /* and a second page */
    bytes[1] = 4;        /* the first page is writable for the rest of the interval */
    visited = 0;
    guest_journal_for_each_open_page(count_page, NULL);
    CHECK(visited == 2);

    const uint64_t second = guest_journal_mark();
    bytes[0] = 5;
    memset(guest_write_pointer(BASE + 2 * page, 8), 6, 8); /* a noted write, as a syscall makes */

    visited = 0;
    guest_journal_rewind(second, count_page, NULL);
    CHECK(visited == 2);
    CHECK(bytes[0] == 2 && bytes[1] == 4 && bytes[page + 1] == 3 && bytes[2 * page] == 0);

    invalidated = 0;
    guest_journal_rewind(start, NULL, NULL);
    CHECK(bytes[0] == 1 && bytes[1] == 0 && bytes[page + 1] == 0);
    CHECK(invalidated == 2);

    /* A rewound timeline runs again from the mark, and a span mapped later is journaled too. */
    const GuestMappingState mapped = guest_memory_mapping_state();
    guest_memory_map(LATE, page);
    const uint64_t again = guest_journal_mark();
    CHECK(again == start);
    bytes[0] = 7;
    g_mem[LATE] = 8;
    guest_journal_release(again);
    guest_journal_rewind(again, NULL, NULL);
    CHECK(bytes[0] == 1 && g_mem[LATE] == 0);
    guest_memory_mapping_restore(mapped);
    spans = 0;
    guest_memory_for_each_span(count_span, NULL);
    CHECK(spans == 1);
    /* Mapped again by the rerun: its pages are journaled once, as before. */
    guest_memory_map(LATE, page);
    g_mem[LATE] = 9;
    visited = 0;
    guest_journal_for_each_open_page(count_page, NULL);
    CHECK(visited == 1);

    printf("guest journal: ok\n");
    return 0;
}
