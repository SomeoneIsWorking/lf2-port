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
 * These limits own guest address ranges, not backing allocations. Desktop reserves
 * the address space; WebAssembly allocates only the spans each arena actually uses.
 *
 * These are #defines rather than an enum because the map extends past 0x7fffffff and
 * enum constants are int -- 0x90000000 in an enum is a negative number and every bounds
 * check against it silently passes, which is the same class of bug all over again. */
#ifndef LF2_GUEST_MAP_H
#define LF2_GUEST_MAP_H

#include <stdint.h>

#define GUEST_STACK_BASE 0x00200000u
#define GUEST_STACK_END 0x00300000u

/* the PE image loads at 0x00400000 and is well under 0x10000000 */
#define GUEST_IMAGE_BASE 0x00400000u
#define GUEST_IMAGE_END 0x10000000u

#define GUEST_HEAP_BASE 0x20000000u
#define GUEST_HEAP_SIZE 0x10000000u
#define GUEST_HEAP_END (GUEST_HEAP_BASE + GUEST_HEAP_SIZE) /* 0x30000000 */

#define GUEST_COM_BASE 0x30000000u
#define GUEST_COM_END 0x40000000u

#define GUEST_TIB_BASE 0x7ffde000u
#define GUEST_TIB_END 0x7ffe0000u

/* Surfaces stop before the Win32 thread information and environment blocks. */
#define GUEST_VRAM_BASE 0x50000000u
#define GUEST_VRAM_SIZE (GUEST_TIB_BASE - GUEST_VRAM_BASE)
#define GUEST_VRAM_END (GUEST_VRAM_BASE + GUEST_VRAM_SIZE) /* 0x7ffde000 */

/* Sound PCM: measured at ~4 MB across 116 buffers. Reserved 256 MB. */
#define GUEST_PCM_BASE 0x90000000u
#define GUEST_PCM_SIZE 0x10000000u
#define GUEST_PCM_END (GUEST_PCM_BASE + GUEST_PCM_SIZE) /* 0xa0000000 */

/* Overlap is a build error, not a runtime surprise. */
#ifdef __cplusplus
#define LF2_MAP_ASSERT static_assert
#else
#define LF2_MAP_ASSERT _Static_assert
#endif
LF2_MAP_ASSERT(GUEST_STACK_END <= GUEST_IMAGE_BASE, "stack overlaps image");
LF2_MAP_ASSERT(GUEST_IMAGE_END <= GUEST_HEAP_BASE, "image overlaps heap");
LF2_MAP_ASSERT(GUEST_HEAP_END <= GUEST_COM_BASE, "heap overlaps COM");
LF2_MAP_ASSERT(GUEST_COM_END <= GUEST_VRAM_BASE, "COM overlaps surfaces");
LF2_MAP_ASSERT(GUEST_VRAM_END <= GUEST_TIB_BASE, "surfaces overlap TIB");
LF2_MAP_ASSERT(GUEST_TIB_END <= GUEST_PCM_BASE, "TIB overlaps PCM");
LF2_MAP_ASSERT(GUEST_VRAM_END <= GUEST_PCM_BASE, "vram overlaps pcm -- this is the bug "
                                                 "that made the game play bitmaps as audio");
LF2_MAP_ASSERT(GUEST_PCM_END <= 0xffffffffu, "pcm past the address space");

#undef LF2_MAP_ASSERT

#endif
