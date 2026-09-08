#include "frame_capture.h"
#include "ddraw_diag.h"
#include "environment.h"
#include "framespec.h"
#include "guest.h"
#include "guest_heap.h"
#include "hostwin.h"
#include "lf2_log.h"
#include "script.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- screen-change detection ----
 * Whether a scripted click actually did anything is not answerable from the key array --
 * every screen reads the same keys -- so LF2_SCREEN_HASH watches the framebuffer instead.
 *
 * The comparison is deliberately coarse. Menus animate (cursors blink, banners scroll), so
 * an exact hash changes every frame and reports nothing useful. Instead a subsampled
 * signature is compared byte-for-byte and a change is reported only when a large fraction
 * of it differs, which is what a screen transition looks like and what local animation
 * does not.
 */
enum { SIG_N = 1024, SCREEN_CHANGE_PCT = 25 };

static void screen_change_check(CaptureFrame capture)
{
    if (!lf2_environment_get(LF2_ENV_SCREEN_HASH) || !capture.pixels || capture.width <= 0 || capture.height <= 0)
        return;

    static uint8_t sig[SIG_N], prev[SIG_N];
    static int have_prev;
    for (int i = 0; i < SIG_N; i++) {
        const int x = (int)((long)i * 7919 % capture.width);
        const int y = (int)((long)i * 6271 % capture.height);
        sig[i] = capture.pixels[(long)y * capture.pitch + x];
    }
    if (!have_prev) {
        memcpy(prev, sig, SIG_N);
        have_prev = 1;
        lf2_log_writef(LF2_LOG_INFO, "ddraw", "screen: first frame %ld\n", capture.number);
        return;
    }
    int diff = 0;
    for (int i = 0; i < SIG_N; i++)
        if (sig[i] != prev[i]) diff++;
    const int pct = diff * 100 / SIG_N;
    if (pct >= SCREEN_CHANGE_PCT) {
        lf2_log_writef(LF2_LOG_INFO, "ddraw", "screen: CHANGED at frame %ld (%d%% of samples)\n", capture.number, pct);
        memcpy(prev, sig, SIG_N);
    }
}

/* Diagnostic dumps go to $LF2_DUMP_DIR, default "scratch". Never an absolute path: this
 * is a committed file in a public repository, and a baked-in home directory is both
 * unusable for anyone else and a leak of the author's layout. */
void frame_capture_path(char *out, size_t n, const char *fmt, ...)
{
    const char *dir = lf2_environment_get(LF2_ENV_DUMP_DIR);
    if (!dir || !*dir) dir = "scratch";
    int k = snprintf(out, n, "%s/", dir);
    if (k < 0 || (size_t)k >= n) {
        out[0] = 0;
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(out + k, n - (size_t)k, fmt, ap);
    va_end(ap);
}

/* Deterministic visual capture: LF2_FRAME_DUMP=1500,1800 writes those presented frames as
 * PPM into $LF2_DUMP_DIR. Screenshotting an X server instead means racing the game's own
 * timing -- two attempts at capturing a match landed on the menu before it -- and cannot
 * run headless at all. Frame numbers are exact, so a capture is reproducible.
 */
/* AN ITEM MAY BE `@screen+N` TOO, and that is not a convenience -- it is the same defect the
 * pad scripts had. A dump frame is a stopwatch aimed at a moving target: render_test asked for
 * frame 2250 to get "a frame with fighters on it", and when the routes stopped waiting 840
 * frames for a front end that was already up, 2250-840 landed somewhere else in the match and
 * the arm failed for a reason that had nothing to do with the renderer. The pad scripts were
 * given screen anchors for exactly this in issue #25; the dumps kept their stopwatches.
 *
 * The grammar lives in runtime/app/framespec.h so tests/test_framespec.c can walk it without
 * booting the game; script_when is the resolver, because it is what knows the screens. */
int hostwin_frame_selected(const char *spec, long frame)
{
    return framespec_matches(spec, frame, script_when);
}
/* A capture aimed at a fixed frame number and a probe that fires off game STATE can
 * disagree, and when they do the picture is of the wrong thing while looking perfectly
 * valid -- an A/B of two spawns produced one arm whose run never reached the match, and the
 * two screenshots would have been compared as if they showed the same experiment. So a
 * probe can ask for the next frame instead, and the capture follows the event. */
static int frame_requested;
void gfx_request_frame_dump(void)
{
    frame_requested = 1;
}

static int frame_wanted(long frame)
{
    if (frame_requested) {
        frame_requested = 0;
        return 1;
    }
    return hostwin_frame_selected(lf2_environment_get(LF2_ENV_FRAME_DUMP), frame);
}

/* LF2_MEM_DUMP=<frame>[,<frame>...] writes the game's whole .data section to
 * data_<frame>.bin in $LF2_DUMP_DIR. Diffing two of them across a single input finds the
 * variable behind an on-screen change when reading the disassembly would mean picking one
 * candidate out of hundreds -- which is how the pre-fight overlay's selection index was
 * located. tools/re/diff_data.py does the comparison.
 *
 * The range is the section's own bounds from the PE header, not a guess: dumping too
 * little would drop the answer and look like "nothing changed". */
enum { DATA_BASE = 0x0044d000, DATA_SIZE = 0xc724 };

/* LF2_HEAP_DUMP=<frame>[,...] snapshots the guest heap in use, for the same
 * before/after diffing as LF2_MEM_DUMP but over the region .data cannot reach.
 * tools/re/diff_data.py --base 0x20000000 reads it. */

static void dump_heap(long frame)
{
    if (!hostwin_frame_selected(lf2_environment_get(LF2_ENV_HEAP_DUMP), frame)) return;
    const uint32_t used = guest_heap_used();
    char path[256];
    frame_capture_path(path, sizeof path, "heap_%06ld.bin", frame);
    FILE *f = fopen(path, "wb");
    if (!f) {
        lf2_log_writef(LF2_LOG_INFO, "ddraw", "heap dump: cannot write %s\n", path);
        return;
    }
    if (!guest_memory_dump(f, GUEST_HEAP_BASE, used)) {
        lf2_log_write(LF2_LOG_ERROR, "ddraw", "heap dump encountered an unmapped span or write failure");
        abort();
    }
    fclose(f);
    lf2_log_writef(LF2_LOG_INFO, "ddraw", "heap dump: wrote %s (%u bytes from %08x)\n", path, used,
                   (unsigned)GUEST_HEAP_BASE);
}

static void dump_data(long frame)
{
    if (!hostwin_frame_selected(lf2_environment_get(LF2_ENV_MEM_DUMP), frame)) return;
    char path[256];
    frame_capture_path(path, sizeof path, "data_%06ld.bin", frame);
    FILE *f = fopen(path, "wb");
    if (!f) {
        lf2_log_writef(LF2_LOG_INFO, "ddraw", "data dump: cannot write %s\n", path);
        return;
    }
    fwrite(guest_pointer(DATA_BASE, DATA_SIZE), 1, DATA_SIZE, f);
    fclose(f);
    lf2_log_writef(LF2_LOG_INFO, "ddraw", "data dump: wrote %s (%d bytes from %08x)\n", path, DATA_SIZE, DATA_BASE);
}

static void dump_frame(CaptureFrame capture)
{
    if (!frame_wanted(capture.number)) return;
    char path[256];
    frame_capture_path(path, sizeof path, "frame_%06ld.ppm", capture.number);
    FILE *f = fopen(path, "wb");
    if (!f) {
        lf2_log_writef(LF2_LOG_INFO, "ddraw", "frame dump: cannot write %s\n", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", capture.width, capture.height);
    for (int y = 0; y < capture.height; y++) {
        const uint32_t *row = (const uint32_t *)(capture.pixels + (size_t)y * (size_t)capture.pitch);
        for (int x = 0; x < capture.width; x++) {
            const uint8_t rgb[3] = {(uint8_t)(row[x] >> 16), (uint8_t)(row[x] >> 8), (uint8_t)row[x]};
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    lf2_log_writef(LF2_LOG_INFO, "ddraw", "frame dump: wrote %s (%dx%d)\n", path, capture.width, capture.height);
}

int ddraw_frame_pixels_wanted(long frame)
{
    return frame_wanted(frame) || lf2_environment_get(LF2_ENV_SCREEN_HASH) != NULL;
}

void frame_capture_present(CaptureFrame frame)
{
    screen_change_check(frame);
    dump_frame(frame);
    dump_data(frame.number);
    dump_heap(frame.number);
}
