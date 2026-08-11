/* Frame specifications: "1200", "@match+282", or a comma-separated list of either.
 *
 * WHY THIS IS A HEADER AND NOT A FUNCTION IN ddraw.c. The same rule as
 * runtime/overrides/geom.h and runtime/video/framelife.h: what can be checked without booting
 * the game must be, and the shipping code INCLUDES this rather than keeping a copy, so
 * tests/test_framespec.c exercises what ships. A parser bug here does not crash and does not
 * print -- it silently declines to dump a frame, and the route that wanted it reports "the run
 * never reached that frame", which reads as a game problem rather than a parsing one. That is
 * exactly the failure that has to be caught offline.
 *
 * The resolver is passed in because the anchors live in runtime/app/script.c, which knows what
 * the game has drawn; this file knows only the grammar. That split is also what lets the test
 * supply screens at fixed frames instead of running a game to produce them.
 */
#ifndef FRAMESPEC_H
#define FRAMESPEC_H

#include <stdlib.h>
#include <string.h>

/* Resolve "@name+n" to a frame. *unresolved is set when the screen has not been seen yet (or
 * the name is not one this build knows), and the item must then match NOTHING -- a spec that
 * cannot be resolved has to stay silent rather than fall back to some frame it might mean. */
typedef long (*framespec_resolve_fn)(const char *item, int *unresolved);

enum { FRAMESPEC_ITEM_MAX = 64 };

static inline int framespec_matches(const char *spec, long frame, framespec_resolve_fn resolve)
{
    if (!spec) return 0;
    for (const char *c = spec; *c; ) {
        if (*c == '@') {
            /* Copy the item out: the resolver takes a NUL-terminated name and the spec may
             * hold several. An item longer than the buffer is truncated rather than allowed to
             * run off it, and a truncated name resolves to nothing, which is the safe way to
             * be wrong here. */
            char item[FRAMESPEC_ITEM_MAX];
            size_t n = 0;
            while (c[n] && c[n] != ',' && c[n] != ' ' && n < sizeof item - 1) n++;
            memcpy(item, c, n);
            item[n] = '\0';
            if (resolve) {
                int unresolved = 0;
                const long v = resolve(item, &unresolved);
                if (!unresolved && v == frame) return 1;
            }
            c += n;
            /* Skip any tail that overflowed the buffer, so the next item still starts at an
             * item boundary rather than in the middle of this one. */
            while (*c && *c != ',' && *c != ' ') c++;
        } else {
            char *end = NULL;
            const long v = strtol(c, &end, 10);
            if (end == c) break;          /* not a number and not an anchor: stop */
            if (v == frame) return 1;
            c = end;
        }
        while (*c == ',' || *c == ' ') c++;
    }
    return 0;
}

#endif
