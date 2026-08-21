/* Device-independent input for the global RmlUi document.
 *
 * The host pump owns SDL events, the input subsystem owns persistent game-action mappings,
 * and this module translates both into UI semantics. The document therefore does not know
 * which keyboard or controller produced Up, Confirm, or Cancel.
 */
#ifndef LF2_RMLUI_INPUT_H
#define LF2_RMLUI_INPUT_H

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <SDL3/SDL.h>

struct RmlUiInputCallbacks {
    void (*activate)(bool controller);
    void (*cancel)();
};

void rmlui_input_reset();
void rmlui_input_block_until_release();

/* Record mapped keyboard/controller edges before the next rendered frame. Returns true when
 * the key belongs to a configured game action and must not also take RmlUi's raw-key route. */
bool rmlui_input_note_event(const SDL_Event &event);

/* Handle pointer events in renderer coordinates. SDL's conversion accounts for window
 * points, drawable pixels, logical presentation, render scale, and the current viewport. */
bool rmlui_input_pointer_event(Rml::Context &context, SDL_Renderer &renderer, const SDL_Event &event);

/* Poll every input device through the shipping mappings. Polling complements event edges:
 * virtual and platform controllers are not guaranteed to emit gamepad-class events. */
void rmlui_input_update(Rml::Context &context, Rml::ElementDocument &document, const RmlUiInputCallbacks &callbacks);

#endif
