/* The guest write journal: the pre-image of every host page of guest memory written since a mark,
 * so a snapshot of guest memory is a mark and loading it is a rewind.
 *
 * Between marks the tracked pages are read-only. The first write to one, from translated code or
 * host code, faults into the journal, which keeps the page and lets the write through. A syscall
 * writing into guest memory cannot fault, so its caller notes the write first (guest_write_pointer).
 * A mark is the journal's sequence number; two marks with nothing written between them are equal. */
#ifndef LF2_GUEST_JOURNAL_H
#define LF2_GUEST_JOURNAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*GuestPageVisitor)(uint32_t address, uint32_t size, void *context);

/* Which spans the journal keeps: nonzero to track the span at `address`. */
typedef int (*GuestSpanFilter)(uint32_t address);

/* Tracks every mapped guest span the filter keeps, from now on. 0, logged, where the platform cannot. */
int guest_journal_start(GuestSpanFilter tracked);
/* Every page writable again, every pre-image dropped. */
void guest_journal_stop(void);
int guest_journal_active(void);
/* The host page size the journal tracks. */
uint32_t guest_journal_page_size(void);

/* Pages written since the last mark. */
void guest_journal_for_each_open_page(GuestPageVisitor visit, void *context);
/* Ends the open interval; the returned mark names the state as it is now. */
uint64_t guest_journal_mark(void);
/* Puts every page back as it was at `mark`, visiting each page it restores. Later marks end. */
void guest_journal_rewind(uint64_t mark, GuestPageVisitor restored, void *context);
/* Pre-images older than `mark` are no longer needed. */
void guest_journal_release(uint64_t mark);

/* A write that will not fault (a syscall) is about to change [address, address + size). */
void guest_journal_note_write(uint32_t address, size_t size);
/* A span mapped after start joins the journal if the filter keeps it. */
void guest_journal_track(uint32_t address, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif
