/* The RmlUi settings screen: the port's own settings UI (issue #70).
 *
 * The renderer choice, the lighting, the depth of field and the device mapping used to be
 * hand-rolled pause-menu rows. The settings screen is a real RmlUi document now -- RML/CSS +
 * data bindings -- rendered by the port's own SDL renderer, on top of the frozen frame while
 * the game is paused. The C API here is the whole boundary: the C++ implementation
 * (runtime/app/rmlui.cpp) owns the RmlUi context and the document, and this header is what
 * the C side (render.c's present, the pause menu) calls.
 *
 * The screen is engine-path only for now: it is rendered into the render target render_present
 * composes into, which is the GPU path. On the software fallback there is no such target, so
 * the SETTINGS item is refused there (the pause menu checks render_gpu_enabled).
 */
#ifndef LF2_RMLUI_H
#define LF2_RMLUI_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise RmlUi on the port's SDL renderer/window and load the settings document.
 * Returns 0 and says why when it could not; safe to call repeatedly. */
int  rmlui_init(SDL_Renderer *r, SDL_Window *w);
void rmlui_shutdown(void);

/* The screen's state. open/close are the pause menu's entry and exit points; while it is
 * active the pause menu draws nothing and its navigation is swallowed, and render_present
 * composites the document over the frozen frame. */
int  rmlui_active(void);
void rmlui_open(void);
void rmlui_close(void);

/* Render the document into the CURRENT render target. The caller sets the target to the
 * composed frame between the game's draw and the present, and the document composites over
 * it. */
void rmlui_render(void);

/* Feed an SDL event to the document. Returns 1 if it consumed the event (a key rebind, a
 * click that the document handled) -- the caller should then not pass the event on to the
 * game. Returns 0 for events the settings screen ignores. */
int  rmlui_event(SDL_Event *e);

#ifdef __cplusplus
}
#endif

#endif
