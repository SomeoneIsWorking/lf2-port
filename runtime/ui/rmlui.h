/* The global RmlUi shell, ported from Dusklight's document/window ownership (issue #70).
 * Escape or the controller menu action opens this document directly on every game screen.
 * The app layer decides whether a match must freeze; this UI owns presentation, navigation,
 * settings, and input mapping. Both native and software render paths composite it.
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

/* Active-document state and lifecycle. */
int  rmlui_active(void);
void rmlui_open(void);
void rmlui_close(void);

/* Render the document into the CURRENT render target. The caller sets the target to the
 * composed frame between the game's draw and the present, and the document composites over
 * it. */
void rmlui_render(void);

/* Feed an SDL event to the active document. All physical input is consumed while visible;
 * non-input window and quit events remain available to the host. */
int  rmlui_event(SDL_Event *e);

#ifdef __cplusplus
}
#endif

#endif
