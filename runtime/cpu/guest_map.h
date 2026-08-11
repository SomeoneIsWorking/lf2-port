/* The guest address-space map -- one place, so two arenas cannot silently overlap.
 *
 * They did. The DirectDraw surface arena started at 0x50000000 and grew unbounded; the
 * sound PCM arena started at 0x60000000, 256 MB above it. A session allocates 393
 * surfaces totalling ~322 MB, so the surface arena ran straight through every sound
 * buffer and the game played bitmap data as audio. Nothing detected it, because a bump
 * allocator with no limit cannot tell "next free page" from "someone else's data".
 *
 * The rules that keep it fixed:
 *   - every arena declares BASE and SIZE here, and nowhere else
 *   - the static assertions below fail the build if two of them overlap
 *   - each allocator refuses past its END rather than walking into its neighbour
 *
 * Sizes are reservations in a lazily-committed 4 GiB mapping, so an unused arena costs
 * nothing resident. Being generous here is free; being wrong is silent corruption.
 *
 * These are #defines rather than an enum because the map extends past 0x7fffffff and
 * enum constants are int -- 0x90000000 in an enum is a negative number and every bounds
 * check against it silently passes, which is the same class of bug all over again. */
#ifndef LF2_GUEST_MAP_H
#define LF2_GUEST_MAP_H

#include <stdint.h>

#define GUEST_STACK_BASE  0x00200000u
#define GUEST_STACK_END   0x00300000u

/* the PE image loads at 0x00400000 and is well under 0x10000000 */
#define GUEST_IMAGE_BASE  0x00400000u
#define GUEST_IMAGE_END   0x10000000u

#define GUEST_HEAP_BASE   0x20000000u
#define GUEST_HEAP_SIZE   0x20000000u
#define GUEST_HEAP_END    (GUEST_HEAP_BASE + GUEST_HEAP_SIZE)     /* 0x40000000 */

/* Surfaces: measured at ~322 MB for a full session. Reserved 1 GiB so growth in the
 * game's own content cannot reach the next arena. */
#define GUEST_VRAM_BASE   0x50000000u
#define GUEST_VRAM_SIZE   0x40000000u
#define GUEST_VRAM_END    (GUEST_VRAM_BASE + GUEST_VRAM_SIZE)     /* 0x90000000 */

/* Sound PCM: measured at ~4 MB across 116 buffers. Reserved 256 MB. */
#define GUEST_PCM_BASE    0x90000000u
#define GUEST_PCM_SIZE    0x10000000u
#define GUEST_PCM_END     (GUEST_PCM_BASE + GUEST_PCM_SIZE)       /* 0xa0000000 */

/* Overlap is a build error, not a runtime surprise. */
_Static_assert(GUEST_STACK_END <= GUEST_IMAGE_BASE, "stack overlaps image");
_Static_assert(GUEST_IMAGE_END <= GUEST_HEAP_BASE,  "image overlaps heap");
_Static_assert(GUEST_HEAP_END  <= GUEST_VRAM_BASE,  "heap overlaps vram");
_Static_assert(GUEST_VRAM_END  <= GUEST_PCM_BASE,   "vram overlaps pcm -- this is the bug "
                                                    "that made the game play bitmaps as audio");
_Static_assert(GUEST_PCM_END   <= 0xffffffffu,      "pcm past the address space");

#endif
