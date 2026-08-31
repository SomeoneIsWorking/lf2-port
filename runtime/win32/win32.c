/* USER32 / GDI32 / ole32 / shell, on SDL3. */
#include "com.h"
#include "guest_ops.h"
#include "hostwin.h"
#include "keyboard.h"
#include "touch_input.h"
#include "window_policy.h"
#ifdef __ANDROID__
#include "android_bridge.h"
#endif
#include "render.h"
#include "script.h"
#include "config.h"
#include "bindings.h"
#include "gamepad.h"
#include "rmlui.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define ARG(n) LD32(R(ESP) + 4 + 4 * (n))

static void ret_stdcall(int nargs, uint32_t value)
{
    R(EAX) = value;
    R(ESP) += 4 + 4u * (unsigned)nargs;
}

HostWin hw;

static void queue_startup_messages(void);
static void push_message(uint32_t msg, uint32_t wparam, uint32_t lparam);
static void queue_message(uint32_t msg, uint32_t wparam, uint32_t lparam);
static uint32_t mouse_lparam(float wx, float wy);
static int port_owns_key(uint32_t vk);

enum {
    WM_KEYDOWN_FWD = 0x0100,
    WM_KEYUP_FWD = 0x0101,
    WM_MOUSEMOVE = 0x0200,
    WM_LBUTTONDOWN = 0x0201,
    WM_LBUTTONUP = 0x0202,
    WM_RBUTTONDOWN = 0x0204,
    WM_RBUTTONUP = 0x0205
};

static int mouse_left_down, mouse_right_down;

/* The pointer in GAME coordinates, kept by the port itself.
 *
 * The game's own 0x004546f0/0x00453cdc are written by its WM_MOUSEMOVE handler, which only
 * runs in the front end -- after loading they go stale, so anything in the game proper that
 * reads them sees a pointer frozen wherever it last was. That is why the first attempt at
 * mouse hit-testing on character selection never fired. */
static int host_ptr_x = -1, host_ptr_y = -1;

/* One-shot edge: true once per physical press, so a held button does not auto-repeat
 * through a menu. Consumed by the reader.
 *
 * It also EXPIRES, which it did not used to, and that was a real bug: the front-end
 * launcher does its own hit-testing through the game's click flag and never calls this, so
 * a click on "game start" left the edge armed. It then sat there through the whole data
 * load and was collected by the first ported menu to look -- the mode menu, with the
 * pointer still resting where "game start" had been, which is inside "VS mode". One click
 * on the launcher started a VS match.
 *
 * A click is a per-frame event. If no ported menu claimed it in the frame it happened, it
 * was not for one, and holding it is how a stale input reaches a screen that did not exist
 * when the button went down. One frame of slack because the menu override and the present
 * do not run in a fixed order within a frame. */
static int mouse_click_pending;
static long mouse_click_frame = -1;

static void mouse_click_arm(void)
{
    mouse_click_pending = 1;
    mouse_click_frame = hostwin_frames();
}

int hostwin_mouse_clicked(void)
{
    const int c = mouse_click_pending && (hostwin_frames() - mouse_click_frame) <= 1;
    mouse_click_pending = 0;
    return c;
}

int hostwin_pointer(int *x, int *y)
{
    if (host_ptr_x < 0) return 0;
    *x = host_ptr_x;
    *y = host_ptr_y;
    return 1;
}
static unsigned autokey_pumps;
/* ---- window ---- */

static void h_RegisterClassA(void)
{
    /* Keep the WNDPROC: the game drives part of its state from window messages. */
    hw.wndproc = LD32(ARG(0) + 4);
    ret_stdcall(1, 1);
}

/* Window mode. The game only ever asks for a fixed-size bordered window, so the choice
 * lives here rather than being something it can express.
 *
 * THE WINDOW IS THE SOURCE OF TRUTH for how wide the game is. It used to be
 * LF2_WIDESCREEN=<w>, read once at startup, which is a developer's escape hatch rather than
 * a feature -- issue #20, and the same objection as gating drop-in coop behind LF2_COOP. The
 * width now follows the window's ASPECT (see hostwin_window_geometry in runtime/video/ddraw.c),
 * so dragging an edge widens the field of view while the game is running.
 *
 * LF2_WINDOW_SIZE=<w>x<h> sets the window's INITIAL size and nothing else. It is not the
 * old knob renamed: it does not name a viewport, it names a window, and the width is
 * derived from it exactly as it is from a window the user resized by hand. It exists
 * because a headless run has nobody to drag an edge -- SDL_VIDEODRIVER=offscreen never
 * delivers a resize -- so it is test scaffolding, which is what the LF2_* namespace is
 * for. */
static void apply_initial_window_size(void)
{
    const char *spec = getenv("LF2_WINDOW_SIZE");
    if (!spec) return;
    int w = 0, h = 0;
    if (sscanf(spec, "%dx%d", &w, &h) != 2 || w < 320 || w > 8192 || h < 200 || h > 8192) {
        fprintf(stderr,
                "LF2_WINDOW_SIZE=\"%s\" is not <w>x<h> with w in 320..8192 and h in "
                "200..8192; the window keeps the %dx%d the game asked for\n",
                spec, hw.win_w, hw.win_h);
        return;
    }
    fprintf(stderr, "window: starting at %dx%d (the game asked for %dx%d)\n", w, h, hw.win_w, hw.win_h);
    hw.win_w = w;
    hw.win_h = h;
}

static void apply_window_mode(void)
{
    const char *mode = getenv("LF2_WINDOW");
    if (!mode) mode = "windowed";

    if (strcmp(mode, "borderless") == 0) {
        SDL_SetWindowBordered(hw.window, false);
    } else if (strcmp(mode, "fullscreen") == 0) {
        SDL_SetWindowBordered(hw.window, false);
        SDL_SetWindowFullscreen(hw.window, true);
    } else if (strcmp(mode, "windowed") != 0) {
        fprintf(stderr,
                "LF2_WINDOW: unknown mode \"%s\" "
                "(windowed, borderless, fullscreen)\n",
                mode);
    }
}

static void toggle_fullscreen(void)
{
    const bool now = (SDL_GetWindowFlags(hw.window) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(hw.window, !now);
    if (now) SDL_SetWindowBordered(hw.window, true);
    else SDL_SetWindowBordered(hw.window, false);
}

static void h_CreateWindowExA(void)
{
    hw.win_w = (int)ARG(6);
    hw.win_h = (int)ARG(7);
    if (hw.win_w <= 0 || hw.win_w > 8192) hw.win_w = 794;
    if (hw.win_h <= 0 || hw.win_h > 8192) hw.win_h = 550;
    apply_initial_window_size();

    window_policy_prepare();
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        abort();
    }
    /* HIGH_PIXEL_DENSITY, or the panel is never reached (issue #56). SDL sizes a window in
     * POINTS and draws it in PIXELS, and without this flag it creates a LOW-DPI window: the
     * two are equal, the drawable really is the point size, and the display server scales the
     * finished frame up to the panel. On a 4K screen at 200% that is a 1080p frame stretched
     * to 2160 rows -- which is exactly the whole-screen upscale issue #41 removed, reappearing
     * one layer further out where nothing in this port could see it.
     *
     * Everything downstream is already in pixels and has been since issue #20: the resize
     * hook takes PIXEL_SIZE_CHANGED rather than RESIZED, the surfaces and the composition are
     * sized from it, and render_present draws into a target the size of the render output. So
     * this flag is the one thing that was missing, and with it the composition follows the
     * panel: at 200% a 794x550-point window is a 1588x1100 drawable, which is a world scale of
     * 2 and the game's own 794 columns of world -- the picture the player asked for, drawn at
     * twice the resolution rather than blown up to it. */
    hw.window =
        SDL_CreateWindow("Little Fighter 2", hw.win_w, hw.win_h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!hw.window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        abort();
    }
#ifdef __ANDROID__
    /* SDL turns its orientation hint into USER_LANDSCAPE after creating a resizable window.
     * Ask the Activity to restore the stronger SENSOR_LANDSCAPE + immersive policy after that
     * write, while the game owns a real native window. */
    if (!android_bridge_enforce_window_policy()) abort();
#endif
    fprintf(stderr, "startup: window created visible\n");
    /* THE GPU RENDERER BY NAME, not whichever SDL picks. SDL's default order puts the
     * OpenGL backend first, and that one has no SDL_GPUDevice -- so SDL_GPURenderState, and
     * with it every shader the HD2D pass is made of, is simply unavailable on it. The
     * default was fine while the port only copied rectangles; it stopped being fine the
     * moment the frame started being lit.
     *
     * Asked for, then checked: if the GPU backend is not there (an old driver, a machine
     * with no Vulkan/Metal/D3D12) the port falls back to SDL's choice and says so, and
     * engine_init reports that the lighting cannot run rather than pretending it did. */
    hw.renderer = SDL_CreateRenderer(hw.window, SDL_GPU_RENDERER);
    if (!hw.renderer) {
        fprintf(stderr,
                "video: the '%s' renderer is unavailable (%s) -- falling back to "
                "SDL's choice. The HD2D pass needs a GPU device and will report "
                "itself off.\n",
                SDL_GPU_RENDERER, SDL_GetError());
        hw.renderer = SDL_CreateRenderer(hw.window, NULL);
    }
    if (!hw.renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        abort();
    }
    render_init(hw.renderer);
    /* Before apply_window_mode: going fullscreen changes the size, and the geometry has to
     * exist before anything can follow a change to it. */
    /* SEEDED FROM THE PIXELS, not from the points the window was asked for. Those differ on a
     * scaled display, and PIXEL_SIZE_CHANGED does not necessarily arrive before the first
     * frame -- so taking hw.win_w/h here would compose the opening frames at the point size
     * and then jump. It says both numbers because a run on a scaled display is the only place
     * they differ, and that is precisely the run nobody here can make (issue #56). */
    {
        int pw = hw.win_w, ph = hw.win_h;
        SDL_GetWindowSizeInPixels(hw.window, &pw, &ph);
        if (pw <= 0 || ph <= 0) {
            pw = hw.win_w;
            ph = hw.win_h;
        }
        fprintf(stderr, "window: %dx%d points -> %dx%d pixels (display scale %.2f)%s\n", hw.win_w, hw.win_h, pw, ph,
                (double)SDL_GetWindowPixelDensity(hw.window),
                (pw == hw.win_w && ph == hw.win_h) ? " -- unscaled, so this run says nothing about HiDPI"
                                                   : " -- a SCALED display: the frame is composed at the pixel size");
        hostwin_window_geometry(pw, ph);
    }
    apply_window_mode();
    hw.hwnd = 0x00010000;
    queue_startup_messages();
    ret_stdcall(12, hw.hwnd);
}

static void h_GetClientRect(void)
{
    uint32_t r = ARG(1);
    if (getenv("LF2_BLT_DEBUG")) fprintf(stderr, "GetClientRect -> %08x (%dx%d)\n", r, hw.width, hw.height);
    ST32(r, 0);
    ST32(r + 4, 0);
    ST32(r + 8, (uint32_t)hw.width);
    ST32(r + 12, (uint32_t)hw.height);
    ret_stdcall(2, 1);
}

static void h_GetSystemMetrics(void)
{
    switch (ARG(0)) {
    case 0: ret_stdcall(1, 1920); return; /* SM_CXSCREEN */
    case 1: ret_stdcall(1, 1080); return; /* SM_CYSCREEN */
    default: ret_stdcall(1, 0); return;
    }
}

/* ---- messages ----
 * SDL events are pumped here and turned into the few window messages the game reads.
 * Keyboard state is served from SDL directly via GetKeyState, which is the game's main
 * input path. */
static int quit_posted;

/* LF2_WINDOW_RESIZE=<frame>:<w>x<h>[,...] -- resize the window part-way through a run.
 *
 * A DIAGNOSTIC, and it exists because the headline of issue #20 is that the field of view
 * changes WHILE THE GAME IS RUNNING, and no scripted run can produce that on its own: a
 * headless run (SDL_VIDEODRIVER=offscreen) has no window manager, so dragging an edge is
 * something no test could ever do and SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED never arrives. A
 * feature whose whole point is a mid-run change, tested only at startup, is a feature whose
 * point is untested.
 *
 * It drives the same entry point the resize event does, not a private path -- the only thing
 * it stands in for is the window manager.
 *
 * Each step fires ONCE, on the first pump at or after its frame, and says so; a step whose
 * frame the run never reaches says that at exit rather than leaving the run looking as though
 * it resized. */
enum { RESIZE_MAX = 8 };
static struct {
    long frame;
    int w, h, fired;
} resizes[RESIZE_MAX];
static int resize_n = -1;

static void pump_scripted_resize(void)
{
    if (resize_n < 0) {
        resize_n = 0;
        const char *spec = getenv("LF2_WINDOW_RESIZE");
        for (const char *c = spec; c && *c;) {
            long f = 0;
            int w = 0, h = 0, used = 0;
            if (sscanf(c, "%ld:%dx%d%n", &f, &w, &h, &used) < 3 || w <= 0 || h <= 0) {
                fprintf(stderr,
                        "LF2_WINDOW_RESIZE: \"%s\" is not <frame>:<w>x<h>; the rest "
                        "of the script is IGNORED and no resize will happen there\n",
                        c);
                break;
            }
            if (resize_n >= RESIZE_MAX) {
                fprintf(stderr,
                        "LF2_WINDOW_RESIZE: more than %d steps; \"%s\" and anything "
                        "after it are IGNORED\n",
                        RESIZE_MAX, c);
                break;
            }
            resizes[resize_n].frame = f;
            resizes[resize_n].w = w;
            resizes[resize_n].h = h;
            resize_n++;
            c += used;
            while (*c == ',' || *c == ' ') c++;
        }
        if (resize_n) fprintf(stderr, "window resize script: %d step(s)\n", resize_n);
    }
    const long f = hostwin_frames();
    for (int i = 0; i < resize_n; i++) {
        if (resizes[i].fired || f < resizes[i].frame) continue;
        resizes[i].fired = 1;
        fprintf(stderr, "window resize script: frame %ld (asked for %ld) -- %dx%d\n", f, resizes[i].frame, resizes[i].w,
                resizes[i].h);
        if (hw.window) SDL_SetWindowSize(hw.window, resizes[i].w, resizes[i].h);
        hostwin_window_geometry(resizes[i].w, resizes[i].h);
    }
}

void window_resize_report(void)
{
    for (int i = 0; i < resize_n; i++)
        if (!resizes[i].fired)
            fprintf(stderr,
                    "window resize script: step %d (frame %ld -> %dx%d) NEVER FIRED "
                    "-- the run ended at frame %ld\n",
                    i, resizes[i].frame, resizes[i].w, resizes[i].h, hostwin_frames());
}

static void pump_autoclick(void)
{
    /* The scripted pointer STAYS WHERE IT WAS PUT. autoclick_state only writes a position
     * while a click's window is open, so seeding these from 0 meant the pointer teleported
     * to the origin between clicks -- and the periodic resend below then told the GAME the
     * mouse was at (0,0), which is outside every menu band. A real mouse does not go home
     * between clicks, and nothing downstream expects one that does. */
    static int last_x = -1, last_y = -1;
    int x = last_x, y = last_y;
    static int was_down, announced;
    if (!input_script_click_configured()) return;
    const int down = input_script_click_state(&x, &y);
    last_x = x;
    last_y = y;
    if (x < 0) return; /* no scripted point yet: nothing to report */

    /* Resend periodically rather than once: a single move pushed before the game starts
     * draining its queue is simply lost. Every pump is far too often -- that floods the
     * ring and starves the render loop -- so this repeats at a slow interval. */
    /* The scripted pointer is the pointer, as far as the rest of the port is concerned.
     * This path built the lparam inline and bypassed both mouse_lparam and
     * hostwin_inject_pointer, so hostwin_pointer() stayed unset for the whole run. */
    host_ptr_x = x;
    host_ptr_y = y;
    const uint32_t lp = ((uint32_t)(y & 0xffff) << 16) | (uint32_t)(x & 0xffff);
    static uint64_t last_sent;
    const uint64_t now_ms = SDL_GetTicks();
    if (!announced || now_ms - last_sent > 500) {
        push_message(WM_MOUSEMOVE, (uint32_t)(down ? 1 : 0), lp);
        last_sent = now_ms;
        announced = 1;
    }
    if (down == was_down) return;
    was_down = down;
    mouse_left_down = down;
    /* A scripted click has to be indistinguishable from a physical one, and this path was
     * missing the port's own click edge -- it pushed the window messages the GAME reads but
     * never armed hostwin_mouse_clicked(), which is what the ported menus read. So every
     * scripted click tested hover and nothing else: a menu whose click did not activate at
     * all still looked correct in a scripted run, because the key script that followed
     * confirmed whatever the hover had selected. */
    if (down) mouse_click_arm();
    push_message(WM_MOUSEMOVE, down ? 1 : 0, lp);
    push_message(down ? WM_LBUTTONDOWN : WM_LBUTTONUP, down ? 1 : 0, lp);
}

static void pump_autokey_messages(void)
{
    if (!input_script_key_configured()) return;
    static uint8_t was_down[256];
    static int msg_debug = -1;
    for (uint32_t vk = 0; vk < 256; vk++) {
        const uint8_t now = input_script_key_down(vk) ? 1 : 0;
        if (now == was_down[vk]) continue;
        was_down[vk] = now;
        if (env_flag("LF2_MSG_DEBUG", &msg_debug))
            fprintf(stderr, "autokey vk=%02x %s (pump %u)\n", vk, now ? "down" : "up", autokey_pumps);
        push_message(now ? WM_KEYDOWN_FWD : WM_KEYUP_FWD, vk, 1);
    }
}

static void keydebug_report(void);
static void keydebug_note(unsigned vk);
static void keydebug_selftest(void);

/* Injection points for the controller UI layer in gamepad.c. Keys go in as real
 * WM_KEYDOWN/WM_KEYUP so code that reacts to messages sees them, and are also reflected in
 * the polled key state, because the game reads both. `down` of -1 on the pointer means
 * "move only, do not touch the buttons". */
static uint8_t injected_keys[256];

void hostwin_inject_key(uint32_t vk, int down)
{
    if (vk > 255) return;
    injected_keys[vk] = down ? 1 : 0;
    keyboard_note(vk, down);
    push_message(down ? WM_KEYDOWN_FWD : WM_KEYUP_FWD, vk, 1);
}

void hostwin_inject_pointer(int x, int y, int down)
{
    /* An injected pointer must be indistinguishable from a physical one to the rest of
     * the port, so it updates hostwin_pointer() exactly as a real motion event does.
     * Without this the scripted pointer moved the GAME's copy but not the port's, and
     * anything reading the port's copy saw a pointer that never moved. */
    host_ptr_x = x;
    host_ptr_y = y;
    const uint32_t lp = ((uint32_t)(y & 0xffff) << 16) | (uint32_t)(x & 0xffff);
    push_message(WM_MOUSEMOVE, (uint32_t)(mouse_left_down ? 1 : 0), lp);
    if (down < 0) return;
    if ((down != 0) == mouse_left_down) return;
    mouse_left_down = down != 0;
    if (mouse_left_down) mouse_click_arm();
    push_message(down ? WM_LBUTTONDOWN : WM_LBUTTONUP, down ? 1 : 0, lp);
}

void hostwin_inject_window_pointer(float x, float y, int down)
{
    const uint32_t lp = mouse_lparam(x, y);
    push_message(WM_MOUSEMOVE, (uint32_t)(mouse_left_down ? 1 : 0), lp);
    if (down < 0 || ((down != 0) == mouse_left_down)) return;
    mouse_left_down = down != 0;
    if (mouse_left_down) mouse_click_arm();
    push_message(down ? WM_LBUTTONDOWN : WM_LBUTTONUP, down ? 1 : 0, lp);
}

int hostwin_injected_key(uint32_t vk)
{
    return vk < 256 && injected_keys[vk];
}

void hostwin_pump(void)
{
    /* LF2_QUIT_AFTER=<frames> posts WM_QUIT once that many frames have been presented.
     * Closing the window from a bare X server does not exercise this: with no window
     * manager the close becomes an XDestroyWindow, SDL then touches a dead window and
     * Xlib kills the process, so the game's own shutdown never runs. This drives the same
     * path the game takes when the user quits. */
    {
        static long qa_frames = -2; /* -2 unread, -1 unset */
        if (qa_frames == -2) {
            const char *qa = getenv("LF2_QUIT_AFTER");
            qa_frames = qa ? strtol(qa, NULL, 10) : -1;
        }
        if (qa_frames >= 0 && hostwin_frames() >= qa_frames) quit_posted = 1;
    }

    static int keydbg = -1;
    if (env_flag("LF2_KEY_DEBUG", &keydbg)) {
        static int pumps;
        if (pumps == 0 && getenv("LF2_KEY_DEBUG_SELFTEST")) keydebug_selftest();
        if (++pumps == 400) keydebug_report();
    }

    pump_autokey_messages();
    pump_autoclick();
    pump_scripted_resize();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        /* Update the port's physical-key ledger before any modal owner consumes the event.
         * Scripted keys already pass through push_message(), but a real Escape key used to
         * be continued below before reaching that function. The scripted UI route therefore
         * passed while a keyboard could never open it. All key-up events must be noted here
         * too, or a key released under RmlUi remains held after the document closes. */
        uint32_t key_msg = 0;
        uint32_t key_vk = 0;
        static int rmlui_debug = -1;
        if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
            key_msg = e.type == SDL_EVENT_KEY_DOWN ? WM_KEYDOWN_FWD : WM_KEYUP_FWD;
            key_vk = keyboard_vk_from_scancode(e.key.scancode);
            if (key_vk) keyboard_note(key_vk, key_msg == WM_KEYDOWN_FWD);
            if (env_flag("LF2_RMLUI_DEBUG", &rmlui_debug))
                fprintf(stderr, "rmlui physical key: vk=%02x %s\n", key_vk,
                        e.type == SDL_EVENT_KEY_DOWN ? "down" : "up");
        }
        gamepad_handle_event(&e); /* controllers may come and go at any time */
        /* The RmlUi settings screen takes its own input while it is up; an event it consumed
         * -- a key rebind, an Escape that closed it -- must not also reach the game's message
         * pump or the pause menu's key ledger. */
        if (rmlui_event(&e)) continue;
        if (touch_input_handle_event(&e, hw.renderer, hw.window, gamepad_any_connected(), hostwin_inject_key,
                                     hostwin_inject_window_pointer))
            continue;
        if (e.type == SDL_EVENT_QUIT) quit_posted = 1;
        /* Alt+Enter is what players expect, and the game cannot ask for it itself. */
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT)) {
            toggle_fullscreen();
            continue;
        }

        /* THE WINDOW DRIVES THE FIELD OF VIEW (issue #20). PIXEL_SIZE_CHANGED rather than
         * RESIZED: on a scaled display the two differ, and everything downstream -- the
         * surfaces, the presentation, the texture -- is in pixels. */
        if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) hostwin_window_geometry(e.window.data1, e.window.data2);

        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            if (e.motion.which == SDL_TOUCH_MOUSEID) continue;
            push_message(WM_MOUSEMOVE, (uint32_t)(mouse_left_down ? 1 : 0), mouse_lparam(e.motion.x, e.motion.y));
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            if (e.button.which == SDL_TOUCH_MOUSEID) continue;
            const int down = (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            const uint32_t lp = mouse_lparam(e.button.x, e.button.y);
            if (e.button.button == SDL_BUTTON_LEFT) {
                if (down && !mouse_left_down) mouse_click_arm();
                mouse_left_down = down;
                push_message(down ? WM_LBUTTONDOWN : WM_LBUTTONUP, down ? 1 : 0, lp);
            } else if (e.button.button == SDL_BUTTON_RIGHT) {
                mouse_right_down = down;
                push_message(down ? WM_RBUTTONDOWN : WM_RBUTTONUP, down ? 2 : 0, lp);
            }
        }
        /* The ledger was updated above. Forward the event only when the port does not own it;
         * Escape and every key under an active RmlUi document stay out of LF2's key array. */
        if (key_vk && !port_owns_key(key_vk)) queue_message(key_msg, key_vk, 1);
    }

    virtual_pad_tick(hostwin_frames());
}

static void fill_msg(uint32_t p, uint32_t msg)
{
    ST32(p, hw.hwnd);
    ST32(p + 4, msg);
    ST32(p + 8, 0);
    ST32(p + 12, 0);
    ST32(p + 16, 0);
    ST32(p + 20, 0);
    ST32(p + 24, 0);
}

enum {
    WM_QUIT = 0x0012,
    WM_MOVE = 0x0003,
    WM_SIZE = 0x0005,
    WM_ACTIVATE = 0x0006,
    WM_ACTIVATEAPP = 0x001C,
    WM_SHOWWINDOW = 0x0018
};

/* Key messages. The port previously delivered only the startup batch, so the game never
 * saw a keystroke as an event -- it could poll GetKeyState but nothing that reacts to
 * WM_KEYDOWN would ever fire. */
/* The game imports no GetCursorPos, so the only way it can learn where the pointer is is
 * the lParam of WM_MOUSEMOVE. Coordinates go in the game's own 794x550 space, not the
 * window's, because the renderer letterboxes. */
enum { WM_CHAR = 0x0102, MSG_RING = 64 };

static struct {
    uint32_t msg, wparam, lparam;
} msg_ring[MSG_RING];
static int ring_head, ring_tail;

static void queue_message(uint32_t msg, uint32_t wparam, uint32_t lparam)
{
    const int next = (ring_tail + 1) % MSG_RING;
    if (next == ring_head) return; /* full: drop rather than overwrite */
    msg_ring[ring_tail].msg = msg;
    msg_ring[ring_tail].wparam = wparam;
    msg_ring[ring_tail].lparam = lparam;
    ring_tail = next;
}

static void push_message(uint32_t msg, uint32_t wparam, uint32_t lparam)
{
    if (msg == WM_KEYDOWN_FWD || msg == WM_KEYUP_FWD) keyboard_note(wparam, msg == WM_KEYDOWN_FWD);
    /* The port's own ledger is updated first, then the message is dropped if the port owns
     * the key -- so the pause menu still sees Escape while the game never does. */
    if ((msg == WM_KEYDOWN_FWD || msg == WM_KEYUP_FWD) && port_owns_key(wparam)) return;
    queue_message(msg, wparam, lparam);
}

/* A real window receives these as it is created and shown, and the game acts on them --
 * its WNDPROC is where it works out the rectangle it blits the back buffer into. With no
 * messages ever delivered that rectangle stayed (0,0,0,0), so the final blit to the
 * primary copied nothing. */
static struct {
    uint32_t msg, wparam, lparam;
} startup_queue[8];
static int startup_head, startup_count;

static void queue_startup_messages(void)
{
    const uint32_t size_lparam = ((uint32_t)hw.height << 16) | (uint32_t)hw.width;
    const struct {
        uint32_t m, w, l;
    } msgs[] = {
        {WM_SHOWWINDOW, 1, 0},  {WM_MOVE, 0, 0},     {WM_SIZE, 0, size_lparam}, /* SIZE_RESTORED */
        {WM_ACTIVATEAPP, 1, 0}, {WM_ACTIVATE, 1, 0},                            /* WA_ACTIVE */
    };
    for (unsigned i = 0; i < sizeof msgs / sizeof msgs[0]; i++) {
        startup_queue[startup_count].msg = msgs[i].m;
        startup_queue[startup_count].wparam = msgs[i].w;
        startup_queue[startup_count].lparam = msgs[i].l;
        startup_count++;
    }
}

/* PeekMessage's last argument decides whether the message is consumed: PM_NOREMOVE (0)
 * leaves it queued. Always removing means a game that peeks before calling GetMessage
 * loses the message entirely -- the peek eats it and the Get returns nothing. */
static int next_queued_message(uint32_t p, int remove)
{
    if (startup_head >= startup_count && ring_head != ring_tail) {
        ST32(p, hw.hwnd);
        ST32(p + 4, msg_ring[ring_head].msg);
        ST32(p + 8, msg_ring[ring_head].wparam);
        ST32(p + 12, msg_ring[ring_head].lparam);
        ST32(p + 16, 0);
        ST32(p + 20, 0);
        ST32(p + 24, 0);
        if (remove) ring_head = (ring_head + 1) % MSG_RING;
        return 1;
    }
    if (startup_head >= startup_count) return 0;
    ST32(p, hw.hwnd);
    ST32(p + 4, startup_queue[startup_head].msg);
    ST32(p + 8, startup_queue[startup_head].wparam);
    ST32(p + 12, startup_queue[startup_head].lparam);
    ST32(p + 16, 0);
    ST32(p + 20, 0);
    ST32(p + 24, 0);
    if (remove) startup_head++;
    return 1;
}

static void h_PeekMessageA(void)
{
    hostwin_pump();
    static int msg_debug = -1;
    if (env_flag("LF2_MSG_DEBUG", &msg_debug)) {
        static uint8_t seen[8];
        const uint32_t f = ARG(4) & 7;
        if (!seen[f]) {
            seen[f] = 1;
            fprintf(stderr, "PeekMessage flags=%u hwnd=%08x filter=%u..%u\n", ARG(4), ARG(1), ARG(2), ARG(3));
        }
    }
    if (quit_posted) {
        fill_msg(ARG(0), WM_QUIT);
        ret_stdcall(5, 1);
        return;
    }
    if (next_queued_message(ARG(0), (int)(ARG(4) & 1))) {
        ret_stdcall(5, 1);
        return;
    }
    ret_stdcall(5, 0);
}

static void h_GetMessageA(void)
{
    hostwin_pump();
    static int msg_debug = -1;
    if (env_flag("LF2_MSG_DEBUG", &msg_debug)) {
        static long n;
        if (++n % 500 == 1) fprintf(stderr, "GetMessage call #%ld\n", n);
    }
    if (quit_posted) {
        fill_msg(ARG(0), WM_QUIT);
        ret_stdcall(4, 0);
        return;
    }
    if (next_queued_message(ARG(0), 1)) {
        ret_stdcall(4, 1);
        return;
    }
    fill_msg(ARG(0), 0);
    ret_stdcall(4, 1);
}

void dump_mem_once(void);

static void h_DispatchMessageA(void)
{
    dump_mem_once();
    const uint32_t p = ARG(0);
    const uint32_t msg = LD32(p + 4);
    static int msg_debug = -1;
    if (env_flag("LF2_MSG_DEBUG", &msg_debug))
        fprintf(stderr, "dispatch msg=%04x wparam=%08x lparam=%08x wndproc=%08x\n", msg, LD32(p + 8), LD32(p + 12),
                hw.wndproc);
    if (msg && hw.wndproc) {
        const uint32_t args[4] = {LD32(p), msg, LD32(p + 8), LD32(p + 12)};
        guest_call(hw.wndproc, args, 4);
    }
    ret_stdcall(1, 0);
}

/* Scripted keys, on a wall clock.
 *
 * An earlier version counted pumps, which was wrong: hostwin_pump runs on every
 * PeekMessage as well as every GetMessage, and the game peeks thousands of times a
 * second, so "hold 8 pumps" lasted well under a millisecond and cycled the key roughly a
 * hundred times a second. No menu can read that as discrete presses.
 *
 *   LF2_AUTOKEY=<vk>[,<vk>...]  cycle through these keys
 *   LF2_AUTOKEY_HOLD=<ms>       how long each press is held (default 150)
 *   LF2_AUTOKEY_EVERY=<ms>      gap between presses (default 1200)
 *   LF2_AUTOKEY_START=<ms>      when to begin (default 6000)
 */
/* LF2_KEY_SCRIPT="<vk>:<frame>[,...]" -- the same shape as LF2_VIRTUAL_PAD, and for the
 * same reason. The wall-clock schedule above drifts with however long the data load takes,
 * so a press aimed at a particular screen can land on the one before or after it; the
 * pre-fight overlay in particular has six items and a blind press lands on whichever one
 * happens to be selected. Presented frames are exact and reproducible, so a frame-scheduled
 * script reaches the same place every run.
 *
 * Keys are held for 8 frames, matching the virtual pad, which is long enough for the game
 * to see a discrete press and short enough not to auto-repeat.
 */
/* The parsers and per-poll state for this script live beside the timing model they share,
 * in runtime/app/script.c (input_script_*); win32.c only polls them. */
static uint32_t mouse_lparam(float wx, float wy)
{
    /* Two steps, and they are in two different spaces.
     *
     * FIRST, the window to the COMPOSITION: undo the placement and the scale the picture was
     * drawn with (lf2_window_to_compose, the exact inverse of lf2_compose_rect). Without this
     * the pointer is in screen pixels while every hit test is in the game's, so in a 1080-row
     * window a click lands about twice as far down the screen as the player aimed.
     *
     * THEN the composition to the GAME'S OWN SCREEN: a fixed-width screen centred on a wider
     * viewport has its content moved right, so the pointer moves left by the same amount.
     * That offset is in composition pixels, which is why it can only be applied second. */
    /* SDL DELIVERS THE POINTER IN POINTS AND THE PICTURE IS PLACED IN PIXELS. They are the
     * same number on an unscaled display and differ by the density on a scaled one, so
     * without this every hit test on a 4K screen is out by that factor -- and silently, since
     * a menu that activates the wrong entry looks like nothing at all in a screenshot. The
     * density is 1.0 wherever this does not apply, so the multiply is a no-op there rather
     * than a special case (issue #56). */
    const float density = hw.window ? SDL_GetWindowPixelDensity(hw.window) : 1.0f;
    float lx = 0, ly = 0;
    lf2_pointer_to_compose(wx, wy, density, &lx, &ly);
    const int x = (int)lx - screen_offset_x(), y = (int)ly;
    host_ptr_x = x;
    host_ptr_y = y;
    return ((uint32_t)(y & 0xffff) << 16) | (uint32_t)(x & 0xffff);
}

/* Counted so the summary can distinguish "no transitions" from "never called". */
static long keydebug_calls;

/* Which keys the game polls is a screen signature: the title screen asks about very
 * different keys than character select. Reporting each key once for the whole run (what
 * this used to do) collapses that timeline to one line and hides every transition, so a
 * sweep is closed when a key repeats and its set printed only when it differs from the
 * previous sweep -- first occurrence plus every change, nothing in between. */
static void keydebug_note(unsigned vk)
{
    static uint8_t cur[256], prev[256];
    static int have_prev, sweeps;
    keydebug_calls++;
    if (cur[vk]) { /* repeat => sweep ended */
        sweeps++;
        if (!have_prev || memcmp(cur, prev, sizeof cur) != 0) {
            fprintf(stderr, "poll set changed (sweep %d):", sweeps);
            for (int i = 0; i < 256; i++)
                if (cur[i]) fprintf(stderr, " %02x", i);
            fprintf(stderr, "\n");
            memcpy(prev, cur, sizeof cur);
            have_prev = 1;
        }
        memset(cur, 0, sizeof cur);
    }
    cur[vk] = 1;
}

/* This game never calls GetKeyState, so the detector above would otherwise ship having
 * never once been seen to fire. LF2_KEY_DEBUG_SELFTEST feeds it two different sweeps,
 * which must produce exactly two "poll set changed" lines. */
static void keydebug_selftest(void)
{
    fprintf(stderr, "LF2_KEY_DEBUG selftest: expect 2 'poll set changed' lines\n");
    const unsigned a[] = {0x0d, 0x20, 0x0d};       /* sweep 1, then repeat */
    const unsigned b[] = {0x68, 0x57, 0x49, 0x68}; /* different set */
    for (unsigned i = 0; i < 3; i++) keydebug_note(a[i]);
    for (unsigned i = 0; i < 4; i++) keydebug_note(b[i]);
    fprintf(stderr, "LF2_KEY_DEBUG selftest: done\n");
}

/* Called from the frame pump, not atexit: registering at exit inside the handler would
 * only arm it once the very thing being measured had already happened, and runs here are
 * ended by SIGTERM anyway. This has to fire *during* a run to be worth anything. */
static void keydebug_report(void)
{
    if (keydebug_calls == 0)
        fprintf(stderr, "LF2_KEY_DEBUG: the game never called GetKeyState, so this trace saw\n"
                        "  NOTHING -- that is not evidence of no input. LF2 keeps its own key\n"
                        "  array at 0x455378, filled from WM_KEYDOWN, and reads that instead.\n"
                        "  To follow input, probe reads of 0x455378 rather than this import.\n");
}

/* Escape is the global RmlUi menu command. While its document is visible, all physical input
 * belongs to that document and must read as released to the guest; otherwise a front-end menu
 * moves behind the modal UI. This is Dusklight's input-block ownership applied at LF2's
 * Win32 boundary. */
static int port_owns_key(uint32_t vk)
{
    return vk == 0x1B || rmlui_active();
}

static void h_GetKeyState(void)
{
    hostwin_pump();
    static int key_debug = -1;
    if (env_flag("LF2_KEY_DEBUG", &key_debug)) keydebug_note(ARG(0) & 0xff);
    if (port_owns_key(ARG(0))) {
        ret_stdcall(1, 0);
        return;
    }
    if (ARG(0) == 0x01) {
        ret_stdcall(1, mouse_left_down ? 0xFF80u : 0u);
        return;
    }
    if (ARG(0) == 0x02) {
        ret_stdcall(1, mouse_right_down ? 0xFF80u : 0u);
        return;
    }
    if (input_script_key_down(ARG(0)) || hostwin_injected_key(ARG(0))) {
        ret_stdcall(1, 0xFF80u);
        return;
    }
    const SDL_Scancode sc = keyboard_scancode_from_vk(ARG(0));
    int n = 0;
    const bool *state = SDL_GetKeyboardState(&n);
    const int down = (sc != SDL_SCANCODE_UNKNOWN && (int)sc < n && state[sc]);
    ret_stdcall(1, down ? 0xFF80u : 0u); /* high bit set while held */
}

/* ---- odds and ends ---- */

static void h_MessageBoxA(void)
{
    /* Logged rather than shown: a modal dialog blocks the run and tells us nothing. */
    fprintf(stderr, "[MessageBox] %s | %s\n", ARG(2) ? (const char *)(g_mem + ARG(2)) : "",
            ARG(1) ? (const char *)(g_mem + ARG(1)) : "");
    ret_stdcall(4, 1);
}

static void h_PostQuitMessage(void)
{
    quit_posted = 1;
    ret_stdcall(1, 0);
}

/* The port asking the game to shut down, through the same path the game's own quit takes --
 * so teardown, the atexit reports and a clean exit status all still happen. */
void hostwin_request_quit(void)
{
    quit_posted = 1;
}
int hostwin_quit_requested(void)
{
    return quit_posted;
}

int hostwin_width(void)
{
    return hw.width;
}
int hostwin_height(void)
{
    return hw.height;
}
static void h_SetRect(void)
{
    uint32_t r = ARG(0);
    ST32(r, ARG(1));
    ST32(r + 4, ARG(2));
    ST32(r + 8, ARG(3));
    ST32(r + 12, ARG(4));
    ret_stdcall(5, 1);
}
static void h_ClientToScreen(void)
{
    if (getenv("LF2_BLT_DEBUG"))
        fprintf(stderr, "ClientToScreen pt=%08x (%d,%d)\n", ARG(1), (int)LD32(ARG(1)), (int)LD32(ARG(1) + 4));
    ret_stdcall(2, 1);
}

static void h_u1_1(void)
{
    ret_stdcall(1, 1);
}
static void h_u1_2(void)
{
    ret_stdcall(2, 1);
}
static void h_u1_3(void)
{
    ret_stdcall(3, 1);
}
static void h_u1_6(void)
{
    ret_stdcall(6, 1);
}
static void h_u1_4_defwnd(void)
{
    ret_stdcall(4, 0);
}

/* ole32: the game only uses COM to reach DirectSound. */
/* No file dialog. Reporting a cancelled dialog is a state the game already handles,
 * whereas aborting here takes the whole process down. */
static void h_GetOpenFileNameA(void)
{
    ret_stdcall(1, 0);
}

static void h_CoInitialize(void)
{
    ret_stdcall(1, 0);
}
/* DirectShow (background music) is not implemented, so this reports failure.
 * A generic COM stub is NOT viable here: stdcall methods pop their own arguments and
 * the count varies per method, so a one-size handler corrupts the guest stack. The
 * real interfaces have to be declared with their true signatures. */
uint32_t dshow_create_graph(void);

static void h_CoCreateInstance(void)
{
    /* CLSID_FilterGraph {e436ebb3-...}: the game's background-music path. */
    const uint32_t clsid = ARG(0);
    if (clsid && LD32(clsid) == 0xe436ebb3u) {
        ST32(ARG(4), dshow_create_graph());
        ret_stdcall(5, DD_OK);
        return;
    }
    ST32(ARG(4), 0);
    ret_stdcall(5, E_NOINTERFACE);
}

typedef void (*Handler)(void);

Handler win32_lookup(const char *dll, const char *name)
{
    static const struct {
        const char *dll, *name;
        Handler fn;
    } T[] = {
        {"USER32.dll", "RegisterClassA", h_RegisterClassA},
        {"USER32.dll", "CreateWindowExA", h_CreateWindowExA},
        {"USER32.dll", "GetClientRect", h_GetClientRect},
        {"USER32.dll", "GetSystemMetrics", h_GetSystemMetrics},
        {"USER32.dll", "PeekMessageA", h_PeekMessageA},
        {"USER32.dll", "GetMessageA", h_GetMessageA},
        {"USER32.dll", "DispatchMessageA", h_DispatchMessageA},
        {"USER32.dll", "TranslateMessage", h_u1_1},
        {"USER32.dll", "DefWindowProcA", h_u1_4_defwnd},
        {"USER32.dll", "GetKeyState", h_GetKeyState},
        {"USER32.dll", "MessageBoxA", h_MessageBoxA},
        {"USER32.dll", "PostQuitMessage", h_PostQuitMessage},
        {"USER32.dll", "PostMessageA", h_u1_4_defwnd},
        {"USER32.dll", "SetRect", h_SetRect},
        {"USER32.dll", "ClientToScreen", h_ClientToScreen},
        {"USER32.dll", "ShowWindow", h_u1_2},
        {"USER32.dll", "UpdateWindow", h_u1_1},
        {"USER32.dll", "InvalidateRect", h_u1_3},
        {"USER32.dll", "DestroyWindow", h_u1_1},
        {"USER32.dll", "LoadCursorA", h_u1_2},
        {"USER32.dll", "LoadIconA", h_u1_2},

        {"USER32.dll", "SetCursor", h_u1_1},
        {"COMDLG32.dll", "GetOpenFileNameA", h_GetOpenFileNameA},
        {"ole32.dll", "CoInitialize", h_CoInitialize},
        {"ole32.dll", "CoCreateInstance", h_CoCreateInstance},
        {"SHELL32.dll", "ShellExecuteA", h_u1_6},
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++)
        if (strcmp(T[i].dll, dll) == 0 && strcmp(T[i].name, name) == 0) return T[i].fn;
    return NULL;
}
