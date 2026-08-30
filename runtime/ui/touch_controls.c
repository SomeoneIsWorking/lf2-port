#include "touch_controls.h"

#include "touch_input.h"

static void control_colour(TouchVisualKind kind, Uint8 *red, Uint8 *green, Uint8 *blue)
{
    *red = 235;
    *green = 241;
    *blue = 246;
    if (kind == TOUCH_VISUAL_ATTACK) {
        *red = 239;
        *green = 95;
        *blue = 95;
    } else if (kind == TOUCH_VISUAL_JUMP) {
        *red = 79;
        *green = 209;
        *blue = 197;
    } else if (kind == TOUCH_VISUAL_DEFEND) {
        *red = 246;
        *green = 190;
        *blue = 72;
    }
}

static void line(SDL_Renderer *renderer, float x1, float y1, float x2, float y2)
{
    SDL_RenderLine(renderer, x1, y1, x2, y2);
}

static void direction_icon(SDL_Renderer *renderer, const SDL_FRect *bounds, TouchVisualKind kind)
{
    const float cx = bounds->x + bounds->w * 0.5f;
    const float cy = bounds->y + bounds->h * 0.5f;
    const float span = bounds->w * 0.22f;
    if (kind == TOUCH_VISUAL_UP || kind == TOUCH_VISUAL_DOWN) {
        const float sign = kind == TOUCH_VISUAL_UP ? -1.0f : 1.0f;
        line(renderer, cx, cy - sign * span, cx, cy + sign * span);
        line(renderer, cx, cy + sign * span, cx - span * 0.65f, cy + sign * span * 0.35f);
        line(renderer, cx, cy + sign * span, cx + span * 0.65f, cy + sign * span * 0.35f);
    } else {
        const float sign = kind == TOUCH_VISUAL_LEFT ? -1.0f : 1.0f;
        line(renderer, cx - sign * span, cy, cx + sign * span, cy);
        line(renderer, cx + sign * span, cy, cx + sign * span * 0.35f, cy - span * 0.65f);
        line(renderer, cx + sign * span, cy, cx + sign * span * 0.35f, cy + span * 0.65f);
    }
}

static void action_icon(SDL_Renderer *renderer, const SDL_FRect *bounds, TouchVisualKind kind)
{
    const float cx = bounds->x + bounds->w * 0.5f;
    const float cy = bounds->y + bounds->h * 0.5f;
    const float span = bounds->w * 0.18f;
    if (kind == TOUCH_VISUAL_PAUSE) {
        const SDL_FRect left = {cx - span, cy - span, span * 0.55f, span * 2.0f};
        const SDL_FRect right = {cx + span * 0.45f, cy - span, span * 0.55f, span * 2.0f};
        SDL_RenderFillRect(renderer, &left);
        SDL_RenderFillRect(renderer, &right);
        return;
    }
    const char *label = kind == TOUCH_VISUAL_ATTACK ? "A" : kind == TOUCH_VISUAL_JUMP ? "J" : "D";
    SDL_RenderDebugText(renderer, cx - 4.0f, cy - 4.0f, label);
}

void touch_controls_render(SDL_Renderer *renderer, SDL_Window *window, int controller_connected)
{
    TouchVisual controls[8];
    const int count = touch_input_visuals(renderer, window, controller_connected, controls, 8);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (int index = 0; index < count; ++index) {
        TouchVisual *control = &controls[index];
        Uint8 red, green, blue;
        control_colour(control->kind, &red, &green, &blue);
        SDL_SetRenderDrawColor(renderer, red, green, blue, control->pressed ? 150 : 68);
        SDL_RenderFillRect(renderer, &control->bounds);
        SDL_SetRenderDrawColor(renderer, red, green, blue, control->pressed ? 255 : 180);
        SDL_RenderRect(renderer, &control->bounds);
        if (control->kind <= TOUCH_VISUAL_RIGHT) direction_icon(renderer, &control->bounds, control->kind);
        else action_icon(renderer, &control->bounds, control->kind);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}
