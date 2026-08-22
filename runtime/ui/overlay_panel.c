#include "overlay_panel.h"

#include "guest.h"
#include "render.h"
#include "ui_rgba.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char lf2_font_sans[];
extern const unsigned int lf2_font_sans_len;
extern const unsigned char lf2_font_overlay_cjk[];
extern const unsigned int lf2_font_overlay_cjk_len;

enum {
    PANEL_VARIANTS = 2 * GEOM_OVERLAY_ITEMS,
    PANEL_TEXT_PX = 13,
};

typedef struct {
    const char *latin_before;
    const char *cjk;
    const char *latin_after;
    int value_row;
} OverlayLabel;

typedef struct {
    int x;
    int y;
    int width;
    int height;
} OverlayRunRect;

static const OverlayLabel LABELS[2][GEOM_OVERLAY_ITEMS] = {
    {
        {"Fight! (", "\xe9\x96\x8b\xe5\xa7\x8b", ")", 0},
        {"Reset All (", "\xe9\x87\x8d\xe6\x96\xb0\xe9\x81\xb8\xe6\x93\x87\xe8\xa7\x92\xe8\x89\xb2", ")", 0},
        {"Reset Random (", "\xe9\x87\x8d\xe6\x96\xb0\xe9\x9a\xa8\xe6\xa9\x9f\xe8\xa7\x92\xe8\x89\xb2", ")", 0},
        {"Background (", "\xe8\x83\x8c\xe6\x99\xaf", "):", 1},
        {"Difficulty (", "\xe9\x9b\xa3\xe5\xba\xa6", "):", 1},
        {"Exit (", "\xe9\x9b\xa2\xe9\x96\x8b", ")", 0},
    },
    {
        {"Fight! (", "\xe9\x96\x8b\xe5\xa7\x8b", ")", 0},
        {"Reset All (", "\xe9\x87\x8d\xe6\x96\xb0\xe9\x81\xb8\xe6\x93\x87\xe8\xa7\x92\xe8\x89\xb2", ")", 0},
        {"Reset Random (", "\xe9\x87\x8d\xe6\x96\xb0\xe9\x9a\xa8\xe6\xa9\x9f\xe8\xa7\x92\xe8\x89\xb2", ")", 0},
        {"Stage (", "\xe9\x97\x9c\xe5\x8d\xa1", "):", 1},
        {"Difficulty (", "\xe9\x9b\xa3\xe5\xba\xa6", "):", 1},
        {"Exit (", "\xe9\x9b\xa2\xe9\x96\x8b", ")", 0},
    },
};

static SDL_Surface *logical_cache[PANEL_VARIANTS];
static SDL_Surface *output_cache;
static int output_width;
static int output_height;
static int output_selected = -1;
static int output_stage = -1;
static int panel_hint_active;
static int panel_selected;
static int panel_stage;
static int panel_ttf_initialized;
static long panel_final_parts;
static long panel_appended;
static long panel_forced_failures;
static OverlayRunRect output_cjk_rects[GEOM_OVERLAY_ITEMS];

static SDL_IOStream *font_stream(const unsigned char *data, unsigned int length)
{ return SDL_IOFromConstMem(data, (size_t)length); }

static TTF_Font *open_font(const unsigned char *data, unsigned int length, float pixels)
{
    SDL_IOStream *io = font_stream(data, length);
    if (!io) return NULL;
    return TTF_OpenFontIO(io, true, pixels);
}

static int rounded_contains(float x, float y, float width, float height, float radius)
{
    const float cx = x < radius ? radius : (x > width - radius ? width - radius : x);
    const float cy = y < radius ? radius : (y > height - radius ? height - radius : y);
    const float dx = x - cx;
    const float dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

static uint32_t panel_colour(float x, float y)
{
    if (!rounded_contains(x, y, OVERLAY_PANEL_W, OVERLAY_PANEL_H, 12.0f)) return 0;
    if (!rounded_contains(x - 2.0f, y - 2.0f, OVERLAY_PANEL_W - 4.0f, OVERLAY_PANEL_H - 4.0f, 10.0f))
        return 0xff3556b4u;
    if (!rounded_contains(x - 6.0f, y - 6.0f, OVERLAY_PANEL_W - 12.0f, OVERLAY_PANEL_H - 12.0f, 7.0f))
        return 0xff172c70u;
    return 0xff3957adu;
}

static void paint_panel_base(SDL_Surface *surface, float scale, int selected)
{
    uint32_t *pixels = surface->pixels;
    const int stride = surface->pitch / (int)sizeof(*pixels);
    int hl = 0, ht = 0, hr = 0, hb = 0;
    overlay_panel_row_bounds(selected, &hl, &ht, &hr, &hb);
    int background_left, background_top, background_right, background_bottom;
    int difficulty_left, difficulty_top, difficulty_right, difficulty_bottom;
    overlay_panel_value_bounds(3, &background_left, &background_top, &background_right, &background_bottom);
    overlay_panel_value_bounds(4, &difficulty_left, &difficulty_top, &difficulty_right, &difficulty_bottom);

    for (int py = 0; py < surface->h; py++) {
        const float y = ((float)py + 0.5f) / scale;
        for (int px = 0; px < surface->w; px++) {
            const float x = ((float)px + 0.5f) / scale;
            uint32_t colour = panel_colour(x, y);
            if ((colour >> 24) && x >= (float)hl && x < (float)hr && y >= (float)ht && y < (float)hb)
                colour = 0xff536fc0u;
            const int in_background = x >= (float)background_left && x < (float)background_right &&
                                      y >= (float)background_top && y < (float)background_bottom;
            const int in_difficulty = x >= (float)difficulty_left && x < (float)difficulty_right &&
                                      y >= (float)difficulty_top && y < (float)difficulty_bottom;
            if (in_background || in_difficulty) colour = 0xff000000u;
            pixels[(size_t)py * (size_t)stride + (size_t)px] = colour;
        }
    }
}

static SDL_Surface *render_run(TTF_Font *font, const char *text, SDL_Color colour)
{
    if (!text[0]) return NULL;
    return TTF_RenderText_Blended(font, text, 0, colour);
}

static int blit_run(SDL_Surface *target, SDL_Surface *run, int *x, int y)
{
    SDL_Rect dst = {*x, y, run->w, run->h};
    if (!SDL_BlitSurface(run, NULL, target, &dst)) return 0;
    *x += run->w;
    return 1;
}

static int label_width(SDL_Surface *before, SDL_Surface *cjk, SDL_Surface *after)
{ return (before ? before->w : 0) + (cjk ? cjk->w : 0) + (after ? after->w : 0); }

static int paint_label(SDL_Surface *panel, TTF_Font *latin, TTF_Font *cjk, const OverlayLabel *label, SDL_Color colour,
                       int row, float scale, OverlayRunRect *cjk_rect)
{
    SDL_Surface *before = render_run(latin, label->latin_before, colour);
    SDL_Surface *middle = render_run(cjk, label->cjk, colour);
    SDL_Surface *after = render_run(latin, label->latin_after, colour);
    int complete = before && middle && after;
    if (complete) {
        const int width_px = label_width(before, middle, after);
        const int row_top = GEOM_OV_ROW_Y[row] - OVERLAY_PANEL_Y;
        const int row_bottom = GEOM_OV_ROW_Y[row + 1] - OVERLAY_PANEL_Y;
        const int latin_ascent = TTF_GetFontAscent(latin);
        const int cjk_ascent = TTF_GetFontAscent(cjk);
        const int ascent = latin_ascent > cjk_ascent ? latin_ascent : cjk_ascent;
        const int latin_descent = TTF_GetFontDescent(latin);
        const int cjk_descent = TTF_GetFontDescent(cjk);
        const int descent = latin_descent < cjk_descent ? latin_descent : cjk_descent;
        const int row_mid = (int)lroundf(((float)(row_top + row_bottom) * 0.5f) * scale);
        const int baseline = row_mid + (ascent + descent) / 2;
        int x = label->value_row ? (int)lroundf((float)OVERLAY_PANEL_LABEL_RIGHT * scale) - width_px
                                 : (panel->w - width_px) / 2;
        complete = blit_run(panel, before, &x, baseline - latin_ascent);
        if (complete && cjk_rect) { *cjk_rect = (OverlayRunRect){x, baseline - cjk_ascent, middle->w, middle->h}; }
        complete = complete && blit_run(panel, middle, &x, baseline - cjk_ascent) &&
                   blit_run(panel, after, &x, baseline - latin_ascent);
    }
    if (before) SDL_DestroySurface(before);
    if (middle) SDL_DestroySurface(middle);
    if (after) SDL_DestroySurface(after);
    return complete;
}

static SDL_Surface *make_panel(float scale, int selected, int stage_mode, OverlayRunRect *cjk_rects)
{
    const int width = (int)lroundf((float)OVERLAY_PANEL_W * scale);
    const int height = (int)lroundf((float)OVERLAY_PANEL_H * scale);
    SDL_Surface *panel = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
    if (!panel) return NULL;
    paint_panel_base(panel, scale, selected);

    TTF_Font *latin = open_font(lf2_font_sans, lf2_font_sans_len, (float)PANEL_TEXT_PX * scale);
    TTF_Font *cjk = open_font(lf2_font_overlay_cjk, lf2_font_overlay_cjk_len, (float)PANEL_TEXT_PX * scale);
    if (!latin || !cjk) {
        fprintf(stderr, "overlay panel: embedded outline face failed to open (%s)\n", SDL_GetError());
        if (latin) TTF_CloseFont(latin);
        if (cjk) TTF_CloseFont(cjk);
        SDL_DestroySurface(panel);
        return NULL;
    }

    for (int row = 0; row < GEOM_OVERLAY_ITEMS; row++) {
        const OverlayLabel *label = &LABELS[stage_mode != 0][row];
        const SDL_Color colour = row == selected ? (SDL_Color){255, 255, 255, 255} : (SDL_Color){159, 190, 255, 255};
        if (!paint_label(panel, latin, cjk, label, colour, row, scale, cjk_rects ? &cjk_rects[row] : NULL)) {
            TTF_CloseFont(latin);
            TTF_CloseFont(cjk);
            SDL_DestroySurface(panel);
            return NULL;
        }
    }

    TTF_CloseFont(latin);
    TTF_CloseFont(cjk);
    return panel;
}

static SDL_Surface *logical_panel(int selected, int stage_mode)
{
    const int slot = (stage_mode != 0) * GEOM_OVERLAY_ITEMS + selected;
    if (!logical_cache[slot]) logical_cache[slot] = make_panel(1.0f, selected, stage_mode, NULL);
    return logical_cache[slot];
}

static SDL_Surface *output_panel(float scale, int selected, int stage_mode)
{
    const int width = (int)lroundf((float)OVERLAY_PANEL_W * scale);
    const int height = (int)lroundf((float)OVERLAY_PANEL_H * scale);
    if (!output_cache || output_width != width || output_height != height || output_selected != selected ||
        output_stage != (stage_mode != 0)) {
        if (output_cache) SDL_DestroySurface(output_cache);
        memset(output_cjk_rects, 0, sizeof(output_cjk_rects));
        output_cache = make_panel(scale, selected, stage_mode, output_cjk_rects);
        output_width = width;
        output_height = height;
        output_selected = selected;
        output_stage = stage_mode != 0;
    }
    return output_cache;
}

static int record_panel(uint32_t dst_pixels, int x, int y, const SDL_Surface *panel)
{
    uint32_t *tile = render_tile_begin(dst_pixels, x, y, OVERLAY_PANEL_W, OVERLAY_PANEL_H, panel->w, panel->h);
    if (!tile) return 0;
    const int stride = panel->pitch / (int)sizeof(uint32_t);
    const uint32_t *source = panel->pixels;
    for (int py = 0; py < panel->h; py++) {
        for (int px = 0; px < panel->w; px++) {
            const uint32_t value = source[(size_t)py * (size_t)stride + (size_t)px];
            tile[(size_t)py * (size_t)panel->w + (size_t)px] = ui_rgba_premultiply(value);
        }
    }
    render_tile_end();
    return 1;
}

static void paint_logical(uint32_t dst_pixels, int dst_w, int dst_h, int dst_pitch, int x, int y,
                          const SDL_Surface *panel)
{
    const uint32_t *source = panel->pixels;
    const int source_stride = panel->pitch / (int)sizeof(uint32_t);
    for (int py = 0; py < panel->h; py++) {
        const int dy = y + py;
        if (dy < 0 || dy >= dst_h) continue;
        uint32_t *destination = (uint32_t *)(g_mem + dst_pixels + (size_t)dy * (size_t)dst_pitch);
        for (int px = 0; px < panel->w; px++) {
            const int dx = x + px;
            if (dx < 0 || dx >= dst_w) continue;
            const uint32_t value = source[(size_t)py * (size_t)source_stride + (size_t)px];
            if (!(value >> 24)) continue;
            destination[dx] = ui_rgba_over_xrgb(value, destination[dx]);
        }
    }
}

static int render_panel(uint32_t dst_pixels, int dst_w, int dst_h, int dst_pitch, int x, int y, int selected,
                        int stage_mode, float output_scale)
{
    if (selected < 0 || selected >= GEOM_OVERLAY_ITEMS) return 0;
    if (!panel_ttf_initialized) {
        if (!TTF_Init()) return 0;
        panel_ttf_initialized = 1;
    }
    if (output_scale < 1.0f) output_scale = 1.0f;
    SDL_Surface *logical = logical_panel(selected, stage_mode);
    if (!logical) return 0;
    if (render_gpu_enabled()) {
        SDL_Surface *output = output_panel(output_scale, selected, stage_mode);
        if (!output || !record_panel(dst_pixels, x, y, output)) return 0;
    }
    paint_logical(dst_pixels, dst_w, dst_h, dst_pitch, x, y, logical);
    return 1;
}

void overlay_panel_hint_set(int selected, int stage_mode)
{
    panel_hint_active = 1;
    panel_selected = selected;
    panel_stage = stage_mode;
    panel_final_parts++;
}

void overlay_panel_hint_clear(void) { panel_hint_active = 0; }

enum OverlayPanelAction overlay_panel_apply(uint32_t dst_pixels, int dst_w, int dst_h, int dst_pitch, int x, int y,
                                            float output_scale)
{
    if (!panel_hint_active) return OVERLAY_PANEL_APPLY_NONE;
    if (panel_selected < 0 || panel_selected >= GEOM_OVERLAY_ITEMS) return OVERLAY_PANEL_APPLY_NONE;
    int panel_x, panel_y;
    overlay_panel_origin(x, y, panel_selected, panel_stage, &panel_x, &panel_y);
    if (getenv("LF2_OVERLAY_PANEL_FORCE_FAILURE")) {
        panel_forced_failures++;
        return OVERLAY_PANEL_APPLY_NONE;
    }
    if (!render_panel(dst_pixels, dst_w, dst_h, dst_pitch, panel_x, panel_y, panel_selected, panel_stage, output_scale))
        return OVERLAY_PANEL_APPLY_NONE;
    panel_appended++;
    return OVERLAY_PANEL_APPENDED;
}

void overlay_panel_report(void)
{
    if (!getenv("LF2_OVERLAY_PANEL_DEBUG")) return;
    const int reported_selected = output_cache ? output_selected : panel_selected;
    const int reported_stage = output_cache ? output_stage : panel_stage;
    fprintf(stderr,
            "overlay panel: %ld native panel(s) appended after %ld final authored part(s); "
            "originals retained; %ld forced failure(s); output raster %dx%d; selected %d; %s label\n",
            panel_appended, panel_final_parts, panel_forced_failures, output_width, output_height, reported_selected,
            reported_stage ? "Stage" : "Background");
    if (output_cache) {
        fprintf(stderr, "overlay panel CJK output rectangles:");
        for (int row = 0; row < GEOM_OVERLAY_ITEMS; row++) {
            const OverlayRunRect *rect = &output_cjk_rects[row];
            fprintf(stderr, " %d=%d,%d,%d,%d", row, rect->x, rect->y, rect->width, rect->height);
        }
        fputc('\n', stderr);
    }
}

void overlay_panel_shutdown(void)
{
    overlay_panel_report();
    for (int i = 0; i < PANEL_VARIANTS; i++) {
        if (logical_cache[i]) {
            SDL_DestroySurface(logical_cache[i]);
            logical_cache[i] = NULL;
        }
    }
    if (output_cache) {
        SDL_DestroySurface(output_cache);
        output_cache = NULL;
    }
    if (panel_ttf_initialized) {
        TTF_Quit();
        panel_ttf_initialized = 0;
    }
}
