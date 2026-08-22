/* RmlUi document lifetime at the render boundary (issue #94).
 *
 * Input callbacks run inside rmlui_render(), so closing (or replacing) the document can
 * happen after a UI frame began but before its update/render phases.  A frame token makes
 * that transition explicit: only the same still-open document generation may continue.
 * This is plain state so the shipping rule can be tested without SDL or RmlUi.
 */
#ifndef LF2_RMLUI_LIFECYCLE_H
#define LF2_RMLUI_LIFECYCLE_H

typedef struct {
    unsigned generation;
    int active;
} RmlUiLifecycle;

static inline void rmlui_lifecycle_init(RmlUiLifecycle *l)
{
    l->generation = 0;
    l->active = 0;
}

static inline void rmlui_lifecycle_open(RmlUiLifecycle *l)
{
    if (l->active) return;
    l->generation++;
    l->active = 1;
}

static inline void rmlui_lifecycle_close(RmlUiLifecycle *l)
{
    if (!l->active) return;
    l->active = 0;
    l->generation++;
}

/* Zero is never a live token because the first open advances the generation. */
static inline unsigned rmlui_lifecycle_frame_begin(const RmlUiLifecycle *l) { return l->active ? l->generation : 0; }

static inline int rmlui_lifecycle_frame_continues(const RmlUiLifecycle *l, unsigned token)
{ return token != 0 && l->active && l->generation == token; }

#endif
