/* Host-side frame lifecycle: presentation, capture/readback coordination, pacing, and
 * orderly SDL teardown. DirectDraw records the frame; this module owns what the host does
 * with that completed frame. */
#include "hostwin.h"

#include "com.h"
#include "ddraw_diag.h"
#include "device_icons.h"
#include "guest.h"
#include "render.h"
#include "rmlui.h"
#include "script.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void menu_click_report(void); /* issue #27: the click flag as the front-end menu sees it */

/* File scope so the quit hook and guest clock can read the presented-frame count. */
static long frames;
static uint32_t frame_src_pixels;
static int frame_src_off;

long hostwin_frames(void) { return frames; }

void frame_source_note(uint32_t pixels, int off)
{
    frame_src_pixels = pixels;
    frame_src_off = off;
}

/* The source is discovered from the game's copy to primary rather than from a fixed surface
 * address. It stays zero until that first copy names the composition. */
uint32_t frame_source_pixels(void) { return frame_src_pixels; }

/* Release SDL explicitly at exit. Leaving it to process teardown is usually harmless, but
 * it means the audio device and window outlive the game's own shutdown, and a diagnostic
 * that runs at exit cannot tell an orderly stop from a crash. */
void hostwin_shutdown(void)
{
    script_report();
    pause_report();
    menu_click_report();
    clock_sites_report();
    window_resize_report();
    if (getenv("LF2_SHUTDOWN_DEBUG")) fprintf(stderr, "shutdown: releasing SDL\n");
    render_shutdown();
    device_icons_shutdown();
    if (hw.texture) {
        SDL_DestroyTexture(hw.texture);
        hw.texture = NULL;
    }
    if (hw.renderer) {
        SDL_DestroyRenderer(hw.renderer);
        hw.renderer = NULL;
    }
    if (hw.window) {
        SDL_DestroyWindow(hw.window);
        hw.window = NULL;
    }
    SDL_Quit();
}

/* LF2_UNPACED=1 runs frames as fast as the machine will do them.
 *
 * The pacer exists so the game runs at the speed a player experiences: the guest clock IS the
 * frame counter, so the game never sleeps of its own accord and every frame would otherwise
 * arrive as fast as the CPU could make it. That is exactly what a scripted test wants. Route
 * tests are frame-numbered end to end -- LF2_QUIT_AFTER, LF2_FRAME_DUMP and every `@screen+N`
 * key are counts, not clocks -- so removing the sleep changes which frames happen not at all,
 * only how long it takes to get to them. A 3000-frame route spent 100 seconds of its 100
 * seconds asleep.
 *
 * IT IS AN EXPLICIT SWITCH AND NOT DERIVED FROM THE VIDEO DRIVER, which was the first idea:
 * "nobody can watch an offscreen surface, so do not pace it". That would silently break the
 * one test that must stay paced. tools/routes/smoke_test.py asserts CPU usage under 50% of a core --
 * the busy-wait guard, which separates a run that honours Sleep (~13%) from one that spins
 * (~96%) -- and that check is only meaningful while the pacer is doing its job. Two headless
 * runs need opposite behaviour and nothing about the environment tells them apart, so the
 * caller says which it wants. */
static int frame_unpaced(void)
{
    static int on = -1;
    if (on < 0) {
        on = getenv("LF2_UNPACED") != NULL;
        if (on)
            fprintf(stderr, "pacing: LF2_UNPACED -- frames run as fast as the machine allows. "
                            "Every route key is a frame count, so this changes how long the "
                            "run takes and nothing about what happens in it.\n");
    }
    return on;
}

/* The guest clock is the frame counter (runtime/win32/imports.c), so the game's main loop is
 * always overdue by its own reckoning and never sleeps. Each present waits until the wall
 * reaches the frame's due time. Waiting until a deadline absorbs nanosleep overshoot, while
 * dropping the anchor during loading prevents loader frames from accumulating timing debt. */
static void frame_pace(void)
{
    static uint64_t anchor_wall, anchor_frame;

    if (frame_unpaced()) return;
    if (lf2_loading_now()) {
        anchor_wall = 0;
        return;
    }

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const uint64_t wall = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    const uint64_t frame = (uint64_t)hostwin_frames();

    if (!anchor_wall) {
        anchor_wall = wall;
        anchor_frame = frame;
        return;
    }
    const uint64_t due = anchor_wall + (frame - anchor_frame) * (uint64_t)GUEST_FRAME_NS;
    if (due > wall) {
        const uint64_t delay = due - wall;
        struct timespec req = {(time_t)(delay / 1000000000ull), (long)(delay % 1000000000ull)};
        nanosleep(&req, NULL);
    } else if (wall - due > 250000000ull) {
        anchor_wall = wall;
        anchor_frame = frame;
    }
}

void hostwin_present(const uint8_t *pixels, int w, int h, int src_pitch)
{
    rwatch_frame();
    if (++frames % 60 == 1) fprintf(stderr, "present #%ld %dx%d renderer=%p\n", frames, w, h, (void *)hw.renderer);

    /* Draw before readback. Reading first returns the previous GPU frame, which masquerades
     * as a clean one-pixel camera or sampling error. */
    static uint32_t *shot;
    static int shot_w, shot_h;
    const uint8_t *shown = pixels;
    int shown_pitch = src_pitch;
    const int gpu = hw.renderer && render_gpu_enabled() && render_present(frame_src_pixels, frame_src_off, w, h);
    int shown_w = w, shown_h = h;

    /* GPU readback is a full pipeline stall, so pay for it only when a frame diagnostic will
     * inspect pixels. The software buffer remains the control for ordinary frames. */
    if (gpu && ddraw_frame_pixels_wanted(frames)) {
        render_output_size(&shown_w, &shown_h);
        if (shown_w <= 0 || shown_h <= 0) {
            shown_w = w;
            shown_h = h;
        }
        if (shot && (shot_w != shown_w || shot_h != shown_h)) {
            free(shot);
            shot = NULL;
        }
        if (!shot) {
            shot = malloc((size_t)shown_w * (size_t)shown_h * 4);
            shot_w = shown_w;
            shot_h = shown_h;
        }
        if (shot && render_readback(shot, shown_w, shown_h, shown_w * 4)) {
            shown = (const uint8_t *)shot;
            shown_pitch = shown_w * 4;
        } else {
            shown_w = w;
            shown_h = h;
        }
    }

    /* Diagnostics inspect what was presented, not the software buffer regardless of which
     * renderer drew the frame. */
    ddraw_frame_diagnostics(shown, shown_w, shown_h, shown_pitch, frames);
    render_frame_reset();
    if (!hw.renderer) return;
    if (gpu) {
        frame_pace();
        return;
    }

    /* The software fallback presents the completed composition as one streaming texture.
     * It cannot reproduce the native renderer's per-quad output scaling because its frame is
     * already flattened, but it remains the renderer's exact A/B control. */
    static int tex_w, tex_h;
    if (hw.texture && (tex_w != w || tex_h != h)) {
        SDL_DestroyTexture(hw.texture);
        hw.texture = NULL;
    }
    if (!hw.texture) {
        hw.texture = SDL_CreateTexture(hw.renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        SDL_SetTextureScaleMode(hw.texture, SDL_SCALEMODE_NEAREST);
        tex_w = w;
        tex_h = h;
    }
    void *dst = NULL;
    int pitch = 0;
    if (SDL_LockTexture(hw.texture, NULL, &dst, &pitch)) {
        for (int y = 0; y < h; y++) {
            uint32_t *row = (uint32_t *)((uint8_t *)dst + (size_t)y * (size_t)pitch);
            const uint32_t *src = (const uint32_t *)(pixels + (size_t)y * (size_t)src_pitch);
            memcpy(row, src, (size_t)w * 4);
        }
        SDL_UnlockTexture(hw.texture);
    }

    SDL_FRect place;
    lf2_compose_rect(w, h, &place);
    SDL_SetRenderDrawColor(hw.renderer, 0, 0, 0, 255);
    SDL_RenderClear(hw.renderer);
    SDL_RenderTexture(hw.renderer, hw.texture, NULL, &place);
    if (rmlui_active()) rmlui_render();
    SDL_RenderPresent(hw.renderer);
    frame_pace();
}
