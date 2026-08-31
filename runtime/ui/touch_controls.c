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

static void fill_triangle(SDL_Renderer *renderer, float x1, float y1, float x2, float y2, float x3, float y3, Uint8 red,
                          Uint8 green, Uint8 blue, Uint8 alpha)
{
    SDL_Vertex vertices[3];
    const SDL_FColor color = {(float)red / 255.0f, (float)green / 255.0f, (float)blue / 255.0f, (float)alpha / 255.0f};
    vertices[0].position = (SDL_FPoint){x1, y1};
    vertices[0].color = color;
    vertices[0].tex_coord = (SDL_FPoint){0, 0};
    vertices[1].position = (SDL_FPoint){x2, y2};
    vertices[1].color = color;
    vertices[1].tex_coord = (SDL_FPoint){0, 0};
    vertices[2].position = (SDL_FPoint){x3, y3};
    vertices[2].color = color;
    vertices[2].tex_coord = (SDL_FPoint){0, 0};
    SDL_RenderGeometry(renderer, NULL, vertices, 3, NULL, 0);
}

static void line(SDL_Renderer *renderer, float x1, float y1, float x2, float y2)
{
    SDL_RenderLine(renderer, x1, y1, x2, y2);
}

static void thick_line(SDL_Renderer *renderer, float x1, float y1, float x2, float y2, float thickness, Uint8 red,
                       Uint8 green, Uint8 blue, Uint8 alpha)
{
    const float dx = x2 - x1;
    const float dy = y2 - y1;
    const float length = SDL_sqrtf(dx * dx + dy * dy);
    if (length <= 0.001f) return;
    const float nx = (-dy / length) * (thickness * 0.5f);
    const float ny = (dx / length) * (thickness * 0.5f);

    SDL_Vertex vertices[4];
    const SDL_FColor color = {(float)red / 255.0f, (float)green / 255.0f, (float)blue / 255.0f, (float)alpha / 255.0f};
    vertices[0].position = (SDL_FPoint){x1 + nx, y1 + ny};
    vertices[0].color = color;
    vertices[0].tex_coord = (SDL_FPoint){0, 0};
    vertices[1].position = (SDL_FPoint){x1 - nx, y1 - ny};
    vertices[1].color = color;
    vertices[1].tex_coord = (SDL_FPoint){0, 0};
    vertices[2].position = (SDL_FPoint){x2 + nx, y2 + ny};
    vertices[2].color = color;
    vertices[2].tex_coord = (SDL_FPoint){0, 0};
    vertices[3].position = (SDL_FPoint){x2 - nx, y2 - ny};
    vertices[3].color = color;
    vertices[3].tex_coord = (SDL_FPoint){0, 0};

    const int indices[6] = {0, 1, 2, 2, 1, 3};
    SDL_RenderGeometry(renderer, NULL, vertices, 4, indices, 6);
}

static void direction_icon(SDL_Renderer *renderer, const SDL_FRect *bounds, TouchVisualKind kind, Uint8 red,
                           Uint8 green, Uint8 blue, Uint8 alpha)
{
    const float cx = bounds->x + bounds->w * 0.5f;
    const float cy = bounds->y + bounds->h * 0.5f;
    const float span = bounds->w * 0.28f;

    if (kind == TOUCH_VISUAL_UP) {
        fill_triangle(renderer, cx, cy - span, cx - span * 0.75f, cy + span * 0.5f, cx + span * 0.75f, cy + span * 0.5f,
                      red, green, blue, alpha);
    } else if (kind == TOUCH_VISUAL_DOWN) {
        fill_triangle(renderer, cx, cy + span, cx - span * 0.75f, cy - span * 0.5f, cx + span * 0.75f, cy - span * 0.5f,
                      red, green, blue, alpha);
    } else if (kind == TOUCH_VISUAL_LEFT) {
        fill_triangle(renderer, cx - span, cy, cx + span * 0.5f, cy - span * 0.75f, cx + span * 0.5f, cy + span * 0.75f,
                      red, green, blue, alpha);
    } else if (kind == TOUCH_VISUAL_RIGHT) {
        fill_triangle(renderer, cx + span, cy, cx - span * 0.5f, cy - span * 0.75f, cx - span * 0.5f, cy + span * 0.75f,
                      red, green, blue, alpha);
    }
}

static void sword_icon(SDL_Renderer *renderer, float cx, float cy, float span, Uint8 red, Uint8 green, Uint8 blue,
                       Uint8 alpha)
{
    const float thick = SDL_max(2.0f, span * 0.16f);
    /* Blade 1 (NW to SE) */
    thick_line(renderer, cx - span * 0.95f, cy - span * 0.95f, cx + span * 0.65f, cy + span * 0.65f, thick, red, green,
               blue, alpha);
    /* Crossguard 1 */
    thick_line(renderer, cx + span * 0.35f - span * 0.35f, cy + span * 0.35f + span * 0.35f,
               cx + span * 0.35f + span * 0.35f, cy + span * 0.35f - span * 0.35f, thick, red, green, blue, alpha);
    /* Pommel 1 */
    thick_line(renderer, cx + span * 0.65f, cy + span * 0.65f, cx + span * 0.95f, cy + span * 0.95f, thick * 1.3f, red,
               green, blue, alpha);

    /* Blade 2 (NE to SW) */
    thick_line(renderer, cx + span * 0.95f, cy - span * 0.95f, cx - span * 0.65f, cy + span * 0.65f, thick, red, green,
               blue, alpha);
    /* Crossguard 2 */
    thick_line(renderer, cx - span * 0.35f - span * 0.35f, cy + span * 0.35f - span * 0.35f,
               cx - span * 0.35f + span * 0.35f, cy + span * 0.35f + span * 0.35f, thick, red, green, blue, alpha);
    /* Pommel 2 */
    thick_line(renderer, cx - span * 0.65f, cy + span * 0.65f, cx - span * 0.95f, cy + span * 0.95f, thick * 1.3f, red,
               green, blue, alpha);
}

static void shield_icon(SDL_Renderer *renderer, float cx, float cy, float span, Uint8 red, Uint8 green, Uint8 blue,
                        Uint8 alpha)
{
    const float top = cy - span * 0.9f;
    const float mid = cy + span * 0.05f;
    const float bot = cy + span * 1.0f;
    const float left = cx - span * 0.85f;
    const float right = cx + span * 0.85f;

    /* Shield body */
    const SDL_FRect upper = {left, top, right - left, mid - top};
    SDL_SetRenderDrawColor(renderer, red, green, blue, (Uint8)(alpha * 0.6f));
    SDL_RenderFillRect(renderer, &upper);
    fill_triangle(renderer, left, mid, right, mid, cx, bot, red, green, blue, (Uint8)(alpha * 0.6f));

    /* Shield outline and central crest */
    SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
    line(renderer, left, top, right, top);
    line(renderer, left, top, left, mid);
    line(renderer, right, top, right, mid);
    line(renderer, left, mid, cx, bot);
    line(renderer, right, mid, cx, bot);
    line(renderer, cx, top, cx, bot);
    line(renderer, left, mid * 0.5f + top * 0.5f, right, mid * 0.5f + top * 0.5f);
}

static void jump_icon(SDL_Renderer *renderer, float cx, float cy, float span, Uint8 red, Uint8 green, Uint8 blue,
                      Uint8 alpha)
{
    const float thick = SDL_max(2.5f, span * 0.22f);
    /* Upper chevron */
    thick_line(renderer, cx - span * 0.85f, cy - span * 0.15f, cx, cy - span * 0.9f, thick, red, green, blue, alpha);
    thick_line(renderer, cx + span * 0.85f, cy - span * 0.15f, cx, cy - span * 0.9f, thick, red, green, blue, alpha);

    /* Lower chevron */
    thick_line(renderer, cx - span * 0.85f, cy + span * 0.65f, cx, cy - span * 0.1f, thick, red, green, blue, alpha);
    thick_line(renderer, cx + span * 0.85f, cy + span * 0.65f, cx, cy - span * 0.1f, thick, red, green, blue, alpha);
}

static void action_icon(SDL_Renderer *renderer, const SDL_FRect *bounds, TouchVisualKind kind, Uint8 red, Uint8 green,
                        Uint8 blue, Uint8 alpha)
{
    const float cx = bounds->x + bounds->w * 0.5f;
    const float cy = bounds->y + bounds->h * 0.5f;
    const float span = bounds->w * 0.24f;

    if (kind == TOUCH_VISUAL_PAUSE) {
        const SDL_FRect left = {cx - span * 0.75f, cy - span * 0.85f, span * 0.5f, span * 1.7f};
        const SDL_FRect right = {cx + span * 0.25f, cy - span * 0.85f, span * 0.5f, span * 1.7f};
        SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
        SDL_RenderFillRect(renderer, &left);
        SDL_RenderFillRect(renderer, &right);
        return;
    }

    if (kind == TOUCH_VISUAL_ATTACK) {
        sword_icon(renderer, cx, cy, span, red, green, blue, alpha);
    } else if (kind == TOUCH_VISUAL_DEFEND) {
        shield_icon(renderer, cx, cy, span, red, green, blue, alpha);
    } else if (kind == TOUCH_VISUAL_JUMP) {
        jump_icon(renderer, cx, cy, span, red, green, blue, alpha);
    }
}

enum { CIRCLE_SEGMENTS = 24 };

static void draw_circle(SDL_Renderer *renderer, float cx, float cy, float radius, Uint8 red, Uint8 green, Uint8 blue,
                        Uint8 alpha)
{
    SDL_Vertex vertices[CIRCLE_SEGMENTS + 2];
    const SDL_FColor color = {(float)red / 255.0f, (float)green / 255.0f, (float)blue / 255.0f, (float)alpha / 255.0f};
    vertices[0].position = (SDL_FPoint){cx, cy};
    vertices[0].color = color;
    vertices[0].tex_coord = (SDL_FPoint){0, 0};

    int indices[CIRCLE_SEGMENTS * 3];
    const float step = (2.0f * SDL_PI_F) / (float)CIRCLE_SEGMENTS;
    for (int i = 0; i < CIRCLE_SEGMENTS; ++i) {
        const float angle = (float)i * step;
        vertices[i + 1].position = (SDL_FPoint){cx + SDL_cosf(angle) * radius, cy + SDL_sinf(angle) * radius};
        vertices[i + 1].color = color;
        vertices[i + 1].tex_coord = (SDL_FPoint){0, 0};

        const int next = (i + 1) % CIRCLE_SEGMENTS;
        indices[i * 3 + 0] = 0;
        indices[i * 3 + 1] = i + 1;
        indices[i * 3 + 2] = next + 1;
    }
    vertices[CIRCLE_SEGMENTS + 1] = vertices[1];
    SDL_RenderGeometry(renderer, NULL, vertices, CIRCLE_SEGMENTS + 2, indices, CIRCLE_SEGMENTS * 3);
}

static void draw_circle_ring(SDL_Renderer *renderer, float cx, float cy, float radius, float thickness, Uint8 red,
                             Uint8 green, Uint8 blue, Uint8 alpha)
{
    const float r_outer = radius;
    const float r_inner = SDL_max(1.0f, radius - thickness);
    SDL_Vertex vertices[CIRCLE_SEGMENTS * 2];
    int indices[CIRCLE_SEGMENTS * 6];
    const SDL_FColor color = {(float)red / 255.0f, (float)green / 255.0f, (float)blue / 255.0f, (float)alpha / 255.0f};

    const float step = (2.0f * SDL_PI_F) / (float)CIRCLE_SEGMENTS;
    for (int i = 0; i < CIRCLE_SEGMENTS; ++i) {
        const float angle = (float)i * step;
        const float cos_a = SDL_cosf(angle);
        const float sin_a = SDL_sinf(angle);

        vertices[i * 2 + 0].position = (SDL_FPoint){cx + cos_a * r_inner, cy + sin_a * r_inner};
        vertices[i * 2 + 0].color = color;
        vertices[i * 2 + 0].tex_coord = (SDL_FPoint){0, 0};

        vertices[i * 2 + 1].position = (SDL_FPoint){cx + cos_a * r_outer, cy + sin_a * r_outer};
        vertices[i * 2 + 1].color = color;
        vertices[i * 2 + 1].tex_coord = (SDL_FPoint){0, 0};

        const int next_i = (i + 1) % CIRCLE_SEGMENTS;
        indices[i * 6 + 0] = i * 2 + 0;
        indices[i * 6 + 1] = i * 2 + 1;
        indices[i * 6 + 2] = next_i * 2 + 1;
        indices[i * 6 + 3] = i * 2 + 0;
        indices[i * 6 + 4] = next_i * 2 + 1;
        indices[i * 6 + 5] = next_i * 2 + 0;
    }
    SDL_RenderGeometry(renderer, NULL, vertices, CIRCLE_SEGMENTS * 2, indices, CIRCLE_SEGMENTS * 6);
}

void touch_controls_render(SDL_Renderer *renderer, SDL_Window *window)
{
    TouchVisual controls[8];
    const int count = touch_input_visuals(renderer, window, controls, 8);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (int index = 0; index < count; ++index) {
        TouchVisual *control = &controls[index];
        Uint8 red, green, blue;
        control_colour(control->kind, &red, &green, &blue);
        const float cx = control->bounds.x + control->bounds.w * 0.5f;
        const float cy = control->bounds.y + control->bounds.h * 0.5f;
        const float radius = control->bounds.w * 0.46f;

        /* Dark background disc for maximum contrast against stages and menus */
        draw_circle(renderer, cx, cy, radius, 14, 20, 30, 175);
        /* Colored fill disc */
        draw_circle(renderer, cx, cy, radius, red, green, blue, control->pressed ? 170 : 65);
        /* Circular outer ring border */
        draw_circle_ring(renderer, cx, cy, radius, control->pressed ? 4.0f : 2.5f, red, green, blue,
                         control->pressed ? 255 : 190);

        const Uint8 icon_alpha = control->pressed ? 255 : 225;
        if (control->kind <= TOUCH_VISUAL_RIGHT)
            direction_icon(renderer, &control->bounds, control->kind, red, green, blue, icon_alpha);
        else action_icon(renderer, &control->bounds, control->kind, red, green, blue, icon_alpha);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}
