#ifndef LF2_TOUCH_INPUT_H
#define LF2_TOUCH_INPUT_H

#include <SDL3/SDL.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*TouchInputEmitKey)(uint32_t virtual_key, int down);

typedef enum TouchVisualKind {
    TOUCH_VISUAL_UP,
    TOUCH_VISUAL_DOWN,
    TOUCH_VISUAL_LEFT,
    TOUCH_VISUAL_RIGHT,
    TOUCH_VISUAL_ATTACK,
    TOUCH_VISUAL_JUMP,
    TOUCH_VISUAL_DEFEND,
    TOUCH_VISUAL_PAUSE,
} TouchVisualKind;

typedef struct TouchVisual {
    SDL_FRect bounds;
    TouchVisualKind kind;
    int pressed;
} TouchVisual;

int touch_input_handle_event(const SDL_Event *event, SDL_Renderer *renderer, SDL_Window *window,
                             int controller_connected, TouchInputEmitKey emit_key);
void touch_input_cancel(TouchInputEmitKey emit_key);
int touch_input_visuals(SDL_Renderer *renderer, SDL_Window *window, int controller_connected, TouchVisual *output,
                        int capacity);

#ifdef __cplusplus
}
#endif

#endif
