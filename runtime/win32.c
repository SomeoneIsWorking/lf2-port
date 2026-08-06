/* USER32 / GDI32 / ole32 / shell, on SDL3. */
#include "com.h"
#include "guest_ops.h"
#include "hostwin.h"
#include "render.h"
#include "script.h"

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
static uint32_t scancode_to_vk(SDL_Scancode sc);
static uint32_t mouse_lparam(float wx, float wy);
static int port_owns_key(uint32_t vk);

enum { WM_KEYDOWN_FWD = 0x0100, WM_KEYUP_FWD = 0x0101,
       WM_MOUSEMOVE = 0x0200, WM_LBUTTONDOWN = 0x0201, WM_LBUTTONUP = 0x0202,
       WM_RBUTTONDOWN = 0x0204, WM_RBUTTONUP = 0x0205 };

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
static int  mouse_click_pending;
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
    *x = host_ptr_x; *y = host_ptr_y;
    return 1;
}
static unsigned autokey_pumps;
void gamepad_handle_event(const SDL_Event *e);
void gamepad_drive_ui(void);
void virtual_pad_tick(long frame);

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
 * width now follows the window's ASPECT (see hostwin_window_geometry in runtime/ddraw.c),
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
        fprintf(stderr, "LF2_WINDOW_SIZE=\"%s\" is not <w>x<h> with w in 320..8192 and h in "
                        "200..8192; the window keeps the %dx%d the game asked for\n",
                spec, hw.win_w, hw.win_h);
        return;
    }
    fprintf(stderr, "window: starting at %dx%d (the game asked for %dx%d)\n",
            w, h, hw.win_w, hw.win_h);
    hw.win_w = w; hw.win_h = h;
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
        fprintf(stderr, "LF2_WINDOW: unknown mode \"%s\" "
                        "(windowed, borderless, fullscreen)\n", mode);
    }
}

static void toggle_fullscreen(void)
{
    const bool now = (SDL_GetWindowFlags(hw.window) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(hw.window, !now);
    if (now) SDL_SetWindowBordered(hw.window, true);
    else     SDL_SetWindowBordered(hw.window, false);
}

static void h_CreateWindowExA(void)
{
    hw.win_w = (int)ARG(6);
    hw.win_h = (int)ARG(7);
    if (hw.win_w <= 0 || hw.win_w > 8192) hw.win_w = 794;
    if (hw.win_h <= 0 || hw.win_h > 8192) hw.win_h = 550;
    apply_initial_window_size();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        abort();
    }
    hw.window = SDL_CreateWindow("Little Fighter 2", hw.win_w, hw.win_h,
                                 SDL_WINDOW_RESIZABLE);
    if (!hw.window) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); abort(); }
    hw.renderer = SDL_CreateRenderer(hw.window, NULL);
    render_init(hw.renderer);
    /* Before apply_window_mode: going fullscreen changes the size, and the geometry has to
     * exist before anything can follow a change to it. */
    hostwin_window_geometry(hw.win_w, hw.win_h);
    apply_window_mode();
    hw.hwnd = 0x00010000;
    queue_startup_messages();
    ret_stdcall(12, hw.hwnd);
}

static void h_GetClientRect(void)
{
    uint32_t r = ARG(1);
    if (getenv("LF2_BLT_DEBUG"))
        fprintf(stderr, "GetClientRect -> %08x (%dx%d)\n", r, hw.width, hw.height);
    ST32(r, 0); ST32(r + 4, 0);
    ST32(r + 8, (uint32_t)hw.width); ST32(r + 12, (uint32_t)hw.height);
    ret_stdcall(2, 1);
}

static void h_GetSystemMetrics(void)
{
    switch (ARG(0)) {
    case 0:  ret_stdcall(1, 1920); return;   /* SM_CXSCREEN */
    case 1:  ret_stdcall(1, 1080); return;   /* SM_CYSCREEN */
    default: ret_stdcall(1, 0); return;
    }
}

/* ---- messages ----
 * SDL events are pumped here and turned into the few window messages the game reads.
 * Keyboard state is served from SDL directly via GetKeyState, which is the game's main
 * input path. */
static int quit_posted;

static int autokey_pressed(uint32_t vk);

/* Scripted keys must also arrive as messages, or they only exercise the polling path and
 * tell us nothing about code that reacts to WM_KEYDOWN. */
/* Scripted pointer, the mouse counterpart of LF2_AUTOKEY:
 *   LF2_AUTOCLICK=<x>,<y>   move there and click on the same schedule as LF2_AUTOKEY. */
/* LF2_AUTOCLICK takes one or more points, separated by semicolons, and steps through
 * them on the same schedule as the keys: "400,220;155,29". A single fixed point cannot
 * drive a sequence of screens whose buttons are in different places. */
/* Scripted input is held this many presented frames: long enough for the game to see a
 * discrete press, short enough not to auto-repeat. Shared by the key and click scripts. */
enum { KEY_SCRIPT_HOLD = 8 };

/* LF2_CLICK_SCRIPT="<x>,<y>:<frame>[;...]" -- the frame-scheduled counterpart, matching
 * LF2_KEY_SCRIPT and LF2_VIRTUAL_PAD. Same reason: a wall-clock schedule drifts with the
 * data load, so a click aimed at one screen can land on another. */
static int click_script_state(int *x, int *y)
{
    const char *spec = getenv("LF2_CLICK_SCRIPT");
    if (!spec) return 0;
    const long frame = hostwin_frames();

    int idx = 0;
    for (const char *c = spec; *c; ) {
        const int px = (int)strtol(c, (char **)&c, 10);
        while (*c == ',' || *c == ' ') c++;
        const int py = (int)strtol(c, (char **)&c, 10);
        const char *when = c;
        if (*c == ':') { c++; when = c; }
        char buf[64];
        while (*c && *c != ';' && *c != ' ') c++;
        size_t n = (size_t)(c - when);
        if (n >= sizeof buf) n = sizeof buf - 1;
        memcpy(buf, when, n); buf[n] = 0;
        while (*c == ';' || *c == ' ') c++;

        const int i = idx++;
        script_seen(SCRIPT_CLICKS, i);
        int un = 0;
        const long at = script_when(buf, &un);
        if (un) continue;              /* its screen has not appeared YET -- not never */

        /* The pointer is placed a few frames early and the button pressed after, because
         * the menu hit-tests where the pointer IS when the click arrives -- moving and
         * clicking on the same frame races the game's own read. */
        if (frame >= at - 4 && frame < at + KEY_SCRIPT_HOLD) {
            *x = px; *y = py;
            if (frame >= at) { script_fired(SCRIPT_CLICKS, i); return 1; }
            return 0;
        }
    }
    return 0;
}

static int autoclick_state(int *x, int *y)
{
    if (getenv("LF2_CLICK_SCRIPT")) return click_script_state(x, y);

    const char *spec = getenv("LF2_AUTOCLICK");
    if (!spec) return 0;

    unsigned points = 1;
    for (const char *c = spec; *c; c++) if (*c == ';') points++;

    static uint64_t start_ms;
    if (!start_ms) start_ms = SDL_GetTicks();
    /* Clicks default to the key schedule but can be given their own. They have to be
     * separable: reaching the game means one click on "game start", then a ~25 s data
     * load, then keys -- on a shared clock the keys are all consumed during the load. */
    const char *s_env = getenv("LF2_AUTOCLICK_START");
    const char *e_env = getenv("LF2_AUTOCLICK_EVERY");
    if (!s_env) s_env = getenv("LF2_AUTOKEY_START");
    if (!e_env) e_env = getenv("LF2_AUTOKEY_EVERY");
    const uint64_t begin = s_env ? (uint64_t)strtoul(s_env, NULL, 10) : 6000;
    const uint64_t every = e_env ? (uint64_t)strtoul(e_env, NULL, 10) : 2500;

    const uint64_t now = SDL_GetTicks() - start_ms;
    if (now < begin) return 0;
    const uint64_t elapsed = now - begin;

    /* Cycling suits probing one screen, but a menu path is one-way: re-clicking the list
     * from the top walks back out again, which reads as the game oscillating between two
     * screens rather than as the script looping. LF2_AUTOCLICK_ONCE walks the list once
     * and then stops clicking. */
    const unsigned step = (unsigned)(elapsed / every);
    if (getenv("LF2_AUTOCLICK_ONCE") && step >= points) return 0;
    const unsigned want = step % points;
    unsigned i = 0;
    for (const char *c = spec; *c; ) {
        const int px = (int)strtol(c, (char **)&c, 10);
        while (*c == ',' || *c == ' ') c++;
        const int py = (int)strtol(c, (char **)&c, 10);
        if (i == want) { *x = px; *y = py; break; }
        i++;
        while (*c == ';' || *c == ' ') c++;
    }
    return (elapsed % every) < 150;                /* button held briefly */
}

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
static struct { long frame; int w, h, fired; } resizes[RESIZE_MAX];
static int resize_n = -1;

static void pump_scripted_resize(void)
{
    if (resize_n < 0) {
        resize_n = 0;
        const char *spec = getenv("LF2_WINDOW_RESIZE");
        for (const char *c = spec; c && *c; ) {
            long f = 0; int w = 0, h = 0, used = 0;
            if (sscanf(c, "%ld:%dx%d%n", &f, &w, &h, &used) < 3 || w <= 0 || h <= 0) {
                fprintf(stderr, "LF2_WINDOW_RESIZE: \"%s\" is not <frame>:<w>x<h>; the rest "
                                "of the script is IGNORED and no resize will happen there\n", c);
                break;
            }
            if (resize_n >= RESIZE_MAX) {
                fprintf(stderr, "LF2_WINDOW_RESIZE: more than %d steps; \"%s\" and anything "
                                "after it are IGNORED\n", RESIZE_MAX, c);
                break;
            }
            resizes[resize_n].frame = f;
            resizes[resize_n].w = w;
            resizes[resize_n].h = h;
            resize_n++;
            c += used;
            while (*c == ',' || *c == ' ') c++;
        }
        if (resize_n)
            fprintf(stderr, "window resize script: %d step(s)\n", resize_n);
    }
    const long f = hostwin_frames();
    for (int i = 0; i < resize_n; i++) {
        if (resizes[i].fired || f < resizes[i].frame) continue;
        resizes[i].fired = 1;
        fprintf(stderr, "window resize script: frame %ld (asked for %ld) -- %dx%d\n",
                f, resizes[i].frame, resizes[i].w, resizes[i].h);
        if (hw.window) SDL_SetWindowSize(hw.window, resizes[i].w, resizes[i].h);
        hostwin_window_geometry(resizes[i].w, resizes[i].h);
    }
}

void window_resize_report(void)
{
    for (int i = 0; i < resize_n; i++)
        if (!resizes[i].fired)
            fprintf(stderr, "window resize script: step %d (frame %ld -> %dx%d) NEVER FIRED "
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
    if (!getenv("LF2_AUTOCLICK") && !getenv("LF2_CLICK_SCRIPT")) return;
    const int down = autoclick_state(&x, &y);
    last_x = x; last_y = y;
    if (x < 0) return;                  /* no scripted point yet: nothing to report */

    /* Resend periodically rather than once: a single move pushed before the game starts
     * draining its queue is simply lost. Every pump is far too often -- that floods the
     * ring and starves the render loop -- so this repeats at a slow interval. */
    /* The scripted pointer is the pointer, as far as the rest of the port is concerned.
     * This path built the lparam inline and bypassed both mouse_lparam and
     * hostwin_inject_pointer, so hostwin_pointer() stayed unset for the whole run. */
    host_ptr_x = x; host_ptr_y = y;
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
    if (!getenv("LF2_AUTOKEY") && !getenv("LF2_KEY_SCRIPT")) return;
    static uint8_t was_down[256];
    for (uint32_t vk = 0; vk < 256; vk++) {
        const uint8_t now = autokey_pressed(vk) ? 1 : 0;
        if (now == was_down[vk]) continue;
        was_down[vk] = now;
        if (getenv("LF2_MSG_DEBUG"))
            fprintf(stderr, "autokey vk=%02x %s (pump %u)\n", vk, now ? "down" : "up", autokey_pumps);
        push_message(now ? WM_KEYDOWN_FWD : WM_KEYUP_FWD, vk, 1);
    }
}

static void keydebug_report(void);
static void keydebug_note(unsigned vk);
static void keydebug_selftest(void);
static void keyboard_drive_ui(void);

/* Injection points for the controller UI layer in gamepad.c. Keys go in as real
 * WM_KEYDOWN/WM_KEYUP so code that reacts to messages sees them, and are also reflected in
 * the polled key state, because the game reads both. `down` of -1 on the pointer means
 * "move only, do not touch the buttons". */
static uint8_t injected_keys[256];

void hostwin_inject_key(uint32_t vk, int down)
{
    if (vk > 255) return;
    injected_keys[vk] = down ? 1 : 0;
    push_message(down ? WM_KEYDOWN_FWD : WM_KEYUP_FWD, vk, 1);
}

void hostwin_inject_pointer(int x, int y, int down)
{
    /* An injected pointer must be indistinguishable from a physical one to the rest of
     * the port, so it updates hostwin_pointer() exactly as a real motion event does.
     * Without this the scripted pointer moved the GAME's copy but not the port's, and
     * anything reading the port's copy saw a pointer that never moved. */
    host_ptr_x = x; host_ptr_y = y;
    const uint32_t lp = ((uint32_t)(y & 0xffff) << 16) | (uint32_t)(x & 0xffff);
    push_message(WM_MOUSEMOVE, (uint32_t)(mouse_left_down ? 1 : 0), lp);
    if (down < 0) return;
    if ((down != 0) == mouse_left_down) return;
    mouse_left_down = down != 0;
    if (mouse_left_down) mouse_click_arm();
    push_message(down ? WM_LBUTTONDOWN : WM_LBUTTONUP, down ? 1 : 0, lp);
}

int hostwin_injected_key(uint32_t vk) { return vk < 256 && injected_keys[vk]; }

void hostwin_pump(void)
{
    /* LF2_QUIT_AFTER=<frames> posts WM_QUIT once that many frames have been presented.
     * Closing the window from a bare X server does not exercise this: with no window
     * manager the close becomes an XDestroyWindow, SDL then touches a dead window and
     * Xlib kills the process, so the game's own shutdown never runs. This drives the same
     * path the game takes when the user quits. */
    {
        static long qa_frames = -2;                 /* -2 unread, -1 unset */
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
        gamepad_handle_event(&e);          /* controllers may come and go at any time */
        if (e.type == SDL_EVENT_QUIT) quit_posted = 1;
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE &&
            (e.key.mod & SDL_KMOD_SHIFT)) quit_posted = 1;
        /* Alt+Enter is what players expect, and the game cannot ask for it itself. */
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_RETURN &&
            (e.key.mod & SDL_KMOD_ALT)) { toggle_fullscreen(); continue; }

        /* THE WINDOW DRIVES THE FIELD OF VIEW (issue #20). PIXEL_SIZE_CHANGED rather than
         * RESIZED: on a scaled display the two differ, and everything downstream -- the
         * surfaces, the presentation, the texture -- is in pixels. */
        if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
            hostwin_window_geometry(e.window.data1, e.window.data2);

        if (e.type == SDL_EVENT_MOUSE_MOTION)
            push_message(WM_MOUSEMOVE, (uint32_t)(mouse_left_down ? 1 : 0),
                         mouse_lparam(e.motion.x, e.motion.y));
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
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
        if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
            const uint32_t vk = scancode_to_vk(e.key.scancode);
            if (vk) push_message(e.type == SDL_EVENT_KEY_DOWN ? WM_KEYDOWN_FWD : WM_KEYUP_FWD,
                                 vk, 1);
        }
    }

    virtual_pad_tick(hostwin_frames());
    gamepad_drive_ui();        /* controller -> the input the menus actually read */
    keyboard_drive_ui();       /* the one keyboard layout does the same */
}

/* The single keyboard layout drives the ported front-end menu the same way a pad does:
 * arrow edges move the selection, attack confirms. The layout's own keys, so the menu
 * and the game agree about what the keyboard is. */
static void keyboard_drive_ui(void)
{
    static uint8_t was[3];
    static const struct { uint8_t vk; int delta; } MAP[] = {
        { 0x26, -1 },          /* up arrow    */
        { 0x28, +1 },          /* down arrow  */
        { 0x5A,  0 },          /* Z = attack -> confirm */
    };
    for (unsigned i = 0; i < sizeof MAP / sizeof MAP[0]; i++) {
        const uint8_t down = (uint8_t)(hostwin_key_held(MAP[i].vk) != 0);
        if (down == was[i]) continue;
        was[i] = down;
        if (!down) continue;
        if (MAP[i].delta) menu_move(MAP[i].delta);
        else              menu_confirm();
    }
}

static void fill_msg(uint32_t p, uint32_t msg)
{
    ST32(p, hw.hwnd); ST32(p + 4, msg);
    ST32(p + 8, 0); ST32(p + 12, 0);
    ST32(p + 16, 0); ST32(p + 20, 0); ST32(p + 24, 0);
}

enum { WM_QUIT = 0x0012, WM_MOVE = 0x0003, WM_SIZE = 0x0005,
       WM_ACTIVATE = 0x0006, WM_ACTIVATEAPP = 0x001C, WM_SHOWWINDOW = 0x0018 };

/* Key messages. The port previously delivered only the startup batch, so the game never
 * saw a keystroke as an event -- it could poll GetKeyState but nothing that reacts to
 * WM_KEYDOWN would ever fire. */
/* The game imports no GetCursorPos, so the only way it can learn where the pointer is is
 * the lParam of WM_MOUSEMOVE. Coordinates go in the game's own 794x550 space, not the
 * window's, because the renderer letterboxes. */
enum { WM_CHAR = 0x0102, MSG_RING = 64 };

static struct { uint32_t msg, wparam, lparam; } msg_ring[MSG_RING];
static int ring_head, ring_tail;

/* Held state for every virtual key, maintained at the ONE point all key sources pass
 * through -- real SDL keys, scripted keys and injected ones alike. The game's own key
 * array (0x455378) is edge-flushed every frame, so it cannot answer "is this key held",
 * which is exactly what the single-layout input routing needs for walking. Updated even
 * when the ring is full: the ring dropping a message must not wedge a key down. */
static uint8_t vk_held[256];
int hostwin_key_held(uint32_t vk) { return vk < 256 && vk_held[vk]; }

static void push_message(uint32_t msg, uint32_t wparam, uint32_t lparam)
{
    if (msg == WM_KEYDOWN_FWD && wparam < 256) vk_held[wparam] = 1;
    if (msg == WM_KEYUP_FWD   && wparam < 256) vk_held[wparam] = 0;
    /* The port's own ledger is updated first, then the message is dropped if the port owns
     * the key -- so the pause menu still sees Escape while the game never does. */
    if ((msg == WM_KEYDOWN_FWD || msg == WM_KEYUP_FWD) && port_owns_key(wparam)) return;
    const int next = (ring_tail + 1) % MSG_RING;
    if (next == ring_head) return;              /* full: drop rather than overwrite */
    msg_ring[ring_tail].msg = msg;
    msg_ring[ring_tail].wparam = wparam;
    msg_ring[ring_tail].lparam = lparam;
    ring_tail = next;
}

/* A real window receives these as it is created and shown, and the game acts on them --
 * its WNDPROC is where it works out the rectangle it blits the back buffer into. With no
 * messages ever delivered that rectangle stayed (0,0,0,0), so the final blit to the
 * primary copied nothing. */
static struct { uint32_t msg, wparam, lparam; } startup_queue[8];
static int startup_head, startup_count;

static void queue_startup_messages(void)
{
    const uint32_t size_lparam = ((uint32_t)hw.height << 16) | (uint32_t)hw.width;
    const struct { uint32_t m, w, l; } msgs[] = {
        { WM_SHOWWINDOW,  1, 0 },
        { WM_MOVE,        0, 0 },
        { WM_SIZE,        0, size_lparam },   /* SIZE_RESTORED */
        { WM_ACTIVATEAPP, 1, 0 },
        { WM_ACTIVATE,    1, 0 },             /* WA_ACTIVE */
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
        ST32(p + 16, 0); ST32(p + 20, 0); ST32(p + 24, 0);
        if (remove) ring_head = (ring_head + 1) % MSG_RING;
        return 1;
    }
    if (startup_head >= startup_count) return 0;
    ST32(p, hw.hwnd);
    ST32(p + 4, startup_queue[startup_head].msg);
    ST32(p + 8, startup_queue[startup_head].wparam);
    ST32(p + 12, startup_queue[startup_head].lparam);
    ST32(p + 16, 0); ST32(p + 20, 0); ST32(p + 24, 0);
    if (remove) startup_head++;
    return 1;
}

static void h_PeekMessageA(void)
{
    hostwin_pump();
    if (getenv("LF2_MSG_DEBUG")) {
        static uint8_t seen[8];
        const uint32_t f = ARG(4) & 7;
        if (!seen[f]) { seen[f] = 1;
            fprintf(stderr, "PeekMessage flags=%u hwnd=%08x filter=%u..%u\n",
                    ARG(4), ARG(1), ARG(2), ARG(3)); }
    }
    if (quit_posted) { fill_msg(ARG(0), WM_QUIT); ret_stdcall(5, 1); return; }
    if (next_queued_message(ARG(0), (int)(ARG(4) & 1))) { ret_stdcall(5, 1); return; }
    ret_stdcall(5, 0);
}

static void h_GetMessageA(void)
{
    hostwin_pump();
    if (getenv("LF2_MSG_DEBUG")) { static long n; if (++n % 500 == 1)
        fprintf(stderr, "GetMessage call #%ld\n", n); }
    if (quit_posted) { fill_msg(ARG(0), WM_QUIT); ret_stdcall(4, 0); return; }
    if (next_queued_message(ARG(0), 1)) { ret_stdcall(4, 1); return; }
    fill_msg(ARG(0), 0);
    ret_stdcall(4, 1);
}

void dump_mem_once(void);

static void h_DispatchMessageA(void)
{
    dump_mem_once();
    const uint32_t p = ARG(0);
    const uint32_t msg = LD32(p + 4);
    if (getenv("LF2_MSG_DEBUG"))
        fprintf(stderr, "dispatch msg=%04x wparam=%08x lparam=%08x wndproc=%08x\n",
                msg, LD32(p + 8), LD32(p + 12), hw.wndproc);
    if (msg && hw.wndproc) {
        const uint32_t args[4] = { LD32(p), msg, LD32(p + 8), LD32(p + 12) };
        guest_call(hw.wndproc, args, 4);
    }
    ret_stdcall(1, 0);
}

/* ---- keyboard ----
 * Virtual-key codes the game polls, mapped to SDL scancodes. */
/* The inverse of vk_to_scancode, for turning SDL key events into window messages. */
static uint32_t scancode_to_vk(SDL_Scancode sc)
{
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
        return (uint32_t)('A' + (sc - SDL_SCANCODE_A));
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9)
        return (uint32_t)('1' + (sc - SDL_SCANCODE_1));
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9)
        return 0x61 + (uint32_t)(sc - SDL_SCANCODE_KP_1);
    switch (sc) {
    case SDL_SCANCODE_0:      return '0';
    case SDL_SCANCODE_KP_0:   return 0x60;
    case SDL_SCANCODE_LEFT:   return 0x25;
    case SDL_SCANCODE_UP:     return 0x26;
    case SDL_SCANCODE_RIGHT:  return 0x27;
    case SDL_SCANCODE_DOWN:   return 0x28;
    case SDL_SCANCODE_RETURN: return 0x0D;
    case SDL_SCANCODE_ESCAPE: return 0x1B;
    case SDL_SCANCODE_SPACE:  return 0x20;
    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return 0x10;
    case SDL_SCANCODE_LCTRL:  case SDL_SCANCODE_RCTRL:  return 0x11;
    case SDL_SCANCODE_LALT:   case SDL_SCANCODE_RALT:   return 0x12;
    case SDL_SCANCODE_TAB:    return 0x09;
    case SDL_SCANCODE_DELETE: return 0x2E;
    case SDL_SCANCODE_F1: case SDL_SCANCODE_F2: case SDL_SCANCODE_F3:
    case SDL_SCANCODE_F4: case SDL_SCANCODE_F5: case SDL_SCANCODE_F6:
        return 0x70 + (uint32_t)(sc - SDL_SCANCODE_F1);
    default: return 0;
    }
}

static SDL_Scancode vk_to_scancode(uint32_t vk)
{
    if (vk >= 'A' && vk <= 'Z') return (SDL_Scancode)(SDL_SCANCODE_A + (vk - 'A'));
    if (vk >= '0' && vk <= '9') return (SDL_Scancode)(SDL_SCANCODE_0 + (vk - '0'));
    switch (vk) {
    case 0x25: return SDL_SCANCODE_LEFT;
    case 0x26: return SDL_SCANCODE_UP;
    case 0x27: return SDL_SCANCODE_RIGHT;
    case 0x28: return SDL_SCANCODE_DOWN;
    case 0x0D: return SDL_SCANCODE_RETURN;
    case 0x1B: return SDL_SCANCODE_ESCAPE;
    case 0x20: return SDL_SCANCODE_SPACE;
    case 0x10: return SDL_SCANCODE_LSHIFT;
    case 0x11: return SDL_SCANCODE_LCTRL;
    case 0x12: return SDL_SCANCODE_LALT;
    case 0x09: return SDL_SCANCODE_TAB;
    case 0x2E: return SDL_SCANCODE_DELETE;
    case 0x60: return SDL_SCANCODE_KP_0;
    case 0x61: return SDL_SCANCODE_KP_1;
    case 0x62: return SDL_SCANCODE_KP_2;
    case 0x63: return SDL_SCANCODE_KP_3;
    case 0x64: return SDL_SCANCODE_KP_4;
    case 0x65: return SDL_SCANCODE_KP_5;
    case 0x66: return SDL_SCANCODE_KP_6;
    case 0x67: return SDL_SCANCODE_KP_7;
    case 0x68: return SDL_SCANCODE_KP_8;
    case 0x69: return SDL_SCANCODE_KP_9;
    default:   return SDL_SCANCODE_UNKNOWN;
    }
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
static int key_script_pressed(uint32_t vk)
{
    const char *script = getenv("LF2_KEY_SCRIPT");
    if (!script) return 0;
    const long frame = hostwin_frames();

    int idx = 0;
    for (const char *c = script; *c; ) {
        const uint32_t key = (uint32_t)strtoul(c, (char **)&c, 16);
        const char *when = c;
        if (*c == ':') { c++; when = c; }
        char buf[64];
        while (*c && *c != ',' && *c != ' ') c++;
        size_t n = (size_t)(c - when);
        if (n >= sizeof buf) n = sizeof buf - 1;
        memcpy(buf, when, n); buf[n] = 0;
        while (*c == ',' || *c == ' ') c++;

        const int i = idx++;
        script_seen(SCRIPT_KEYS, i);
        int un = 0;
        const long at = script_when(buf, &un);
        if (un) continue;              /* its screen has not appeared YET -- not never */
        if (frame < at || frame >= at + KEY_SCRIPT_HOLD) continue;

        /* Recorded for every item whose window this frame is in, not only the one being
         * asked about: this is polled per-vk, and an item's own key being queried is a
         * property of the caller's loop rather than of the script. */
        script_fired(SCRIPT_KEYS, i);
        if (key == vk) return 1;
    }
    return 0;
}

static int autokey_pressed(uint32_t vk)
{
    if (key_script_pressed(vk)) return 1;

    const char *script = getenv("LF2_AUTOKEY");
    if (!script) return 0;

    /* With LF2_AUTOKEY_AFTER the clock starts when the game is first seen polling that
     * key, not at process start -- so the script tracks the game's state instead of
     * drifting with however long the data load happened to take. */
    static uint64_t base_ms;
    if (getenv("LF2_AUTOKEY_AFTER")) {
        if (!rwatch_triggered()) return 0;
        if (!base_ms) base_ms = SDL_GetTicks();
    } else if (!base_ms) {
        base_ms = SDL_GetTicks();
    }
    const char *s_env = getenv("LF2_AUTOKEY_START");
    const char *e_env = getenv("LF2_AUTOKEY_EVERY");
    const char *h_env = getenv("LF2_AUTOKEY_HOLD");
    const uint64_t begin = s_env ? strtoull(s_env, NULL, 10) : 6000;
    const uint64_t every = e_env ? strtoull(e_env, NULL, 10) : 1200;
    const uint64_t hold  = h_env ? strtoull(h_env, NULL, 10) : 150;

    const uint64_t now = SDL_GetTicks() - base_ms;
    if (now < begin) return 0;
    const uint64_t elapsed = now - begin;
    if (elapsed % every >= hold) return 0;

    unsigned count = 0;
    for (const char *c = script; *c; ) {
        (void)strtoul(c, (char **)&c, 16);
        count++;
        while (*c == ',' || *c == ' ') c++;
    }
    if (!count) return 0;

    /* As with clicks, a menu path is one-way: cycling the list keeps navigating and
     * overshoots the screen you were aiming for. LF2_AUTOKEY_ONCE plays it once. */
    const unsigned step = (unsigned)(elapsed / every);
    if (getenv("LF2_AUTOKEY_ONCE") && step >= count) return 0;
    const unsigned want = step % count;
    unsigned i = 0;
    for (const char *c = script; *c; ) {
        const uint32_t key = (uint32_t)strtoul(c, (char **)&c, 16);
        if (i == want) return key == vk;
        i++;
        while (*c == ',' || *c == ' ') c++;
    }
    return 0;
}

static uint32_t mouse_lparam(float wx, float wy)
{
    float lx = wx, ly = wy;
    if (hw.renderer) SDL_RenderCoordinatesFromWindow(hw.renderer, wx, wy, &lx, &ly);
    /* Into the game's own coordinate space: when a fixed-width screen is being centred on a
     * wider viewport its content moved right, so the pointer has to move left by the same
     * amount or every hit test is off by the margin. */
    const int x = (int)lx - screen_offset_x(), y = (int)ly;
    host_ptr_x = x; host_ptr_y = y;
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
    if (cur[vk]) {                                  /* repeat => sweep ended */
        sweeps++;
        if (!have_prev || memcmp(cur, prev, sizeof cur) != 0) {
            fprintf(stderr, "poll set changed (sweep %d):", sweeps);
            for (int i = 0; i < 256; i++) if (cur[i]) fprintf(stderr, " %02x", i);
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
    const unsigned a[] = { 0x0d, 0x20, 0x0d };            /* sweep 1, then repeat */
    const unsigned b[] = { 0x68, 0x57, 0x49, 0x68 };      /* different set */
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
        fprintf(stderr,
                "LF2_KEY_DEBUG: the game never called GetKeyState, so this trace saw\n"
                "  NOTHING -- that is not evidence of no input. LF2 keeps its own key\n"
                "  array at 0x455378, filled from WM_KEYDOWN, and reads that instead.\n"
                "  To follow input, probe reads of 0x455378 rather than this import.\n");
}

/* Escape belongs to the pause menu while a match is on screen.
 *
 * The game reads Escape itself and answers with its own "Are you sure to quit?" prompt, so
 * a pause menu bound to it would open underneath a quit dialog. The port takes the key
 * instead: it is reported as UP to the game whenever the pause menu is entitled to it, and
 * the port's own hostwin_key_held() -- which the pause menu reads -- is unaffected.
 *
 * Only during a match, so Escape still quits from the menus, which is where the game's
 * prompt makes sense. */
static int port_owns_key(uint32_t vk)
{
    return vk == 0x1B && (panel_hud_up() || pause_active());
}

static void h_GetKeyState(void)
{
    hostwin_pump();
    if (getenv("LF2_KEY_DEBUG")) keydebug_note(ARG(0) & 0xff);
    if (port_owns_key(ARG(0))) { ret_stdcall(1, 0); return; }
    if (ARG(0) == 0x01) { ret_stdcall(1, mouse_left_down ? 0xFF80u : 0u); return; }
    if (ARG(0) == 0x02) { ret_stdcall(1, mouse_right_down ? 0xFF80u : 0u); return; }
    if (autokey_pressed(ARG(0)) || hostwin_injected_key(ARG(0))) {
        ret_stdcall(1, 0xFF80u); return;
    }
    const SDL_Scancode sc = vk_to_scancode(ARG(0));
    int n = 0;
    const bool *state = SDL_GetKeyboardState(&n);
    const int down = (sc != SDL_SCANCODE_UNKNOWN && (int)sc < n && state[sc]);
    ret_stdcall(1, down ? 0xFF80u : 0u);   /* high bit set while held */
}

/* ---- odds and ends ---- */

static void h_MessageBoxA(void)
{
    /* Logged rather than shown: a modal dialog blocks the run and tells us nothing. */
    fprintf(stderr, "[MessageBox] %s | %s\n",
            ARG(2) ? (const char *)(g_mem + ARG(2)) : "",
            ARG(1) ? (const char *)(g_mem + ARG(1)) : "");
    ret_stdcall(4, 1);
}

static void h_PostQuitMessage(void) { quit_posted = 1; ret_stdcall(1, 0); }

/* The port asking the game to shut down, through the same path the game's own quit takes --
 * so teardown, the atexit reports and a clean exit status all still happen. */
void hostwin_request_quit(void) { quit_posted = 1; }

int hostwin_width(void)  { return hw.width; }
int hostwin_height(void) { return hw.height; }
static void h_SetRect(void)
{
    uint32_t r = ARG(0);
    ST32(r, ARG(1)); ST32(r + 4, ARG(2)); ST32(r + 8, ARG(3)); ST32(r + 12, ARG(4));
    ret_stdcall(5, 1);
}
static void h_ClientToScreen(void)
{
    if (getenv("LF2_BLT_DEBUG"))
        fprintf(stderr, "ClientToScreen pt=%08x (%d,%d)\n", ARG(1),
                (int)LD32(ARG(1)), (int)LD32(ARG(1) + 4));
    ret_stdcall(2, 1);
}

static void h_u1_1(void) { ret_stdcall(1, 1); }
static void h_u1_2(void) { ret_stdcall(2, 1); }
static void h_u1_3(void) { ret_stdcall(3, 1); }
static void h_u1_6(void) { ret_stdcall(6, 1); }
static void h_u1_4_defwnd(void) { ret_stdcall(4, 0); }

/* ole32: the game only uses COM to reach DirectSound. */
/* No file dialog. Reporting a cancelled dialog is a state the game already handles,
 * whereas aborting here takes the whole process down. */
static void h_GetOpenFileNameA(void) { ret_stdcall(1, 0); }

static void h_CoInitialize(void)   { ret_stdcall(1, 0); }
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
    static const struct { const char *dll, *name; Handler fn; } T[] = {
        { "USER32.dll", "RegisterClassA",    h_RegisterClassA },
        { "USER32.dll", "CreateWindowExA",   h_CreateWindowExA },
        { "USER32.dll", "GetClientRect",     h_GetClientRect },
        { "USER32.dll", "GetSystemMetrics",  h_GetSystemMetrics },
        { "USER32.dll", "PeekMessageA",      h_PeekMessageA },
        { "USER32.dll", "GetMessageA",       h_GetMessageA },
        { "USER32.dll", "DispatchMessageA",  h_DispatchMessageA },
        { "USER32.dll", "TranslateMessage",  h_u1_1 },
        { "USER32.dll", "DefWindowProcA",    h_u1_4_defwnd },
        { "USER32.dll", "GetKeyState",       h_GetKeyState },
        { "USER32.dll", "MessageBoxA",       h_MessageBoxA },
        { "USER32.dll", "PostQuitMessage",   h_PostQuitMessage },
        { "USER32.dll", "PostMessageA",      h_u1_4_defwnd },
        { "USER32.dll", "SetRect",           h_SetRect },
        { "USER32.dll", "ClientToScreen",    h_ClientToScreen },
        { "USER32.dll", "ShowWindow",        h_u1_2 },
        { "USER32.dll", "UpdateWindow",      h_u1_1 },
        { "USER32.dll", "InvalidateRect",    h_u1_3 },
        { "USER32.dll", "DestroyWindow",     h_u1_1 },
        { "USER32.dll", "LoadCursorA",       h_u1_2 },
        { "USER32.dll", "LoadIconA",         h_u1_2 },

        { "USER32.dll", "SetCursor",         h_u1_1 },
        { "COMDLG32.dll", "GetOpenFileNameA", h_GetOpenFileNameA },
        { "ole32.dll",  "CoInitialize",      h_CoInitialize },
        { "ole32.dll",  "CoCreateInstance",  h_CoCreateInstance },
        { "SHELL32.dll", "ShellExecuteA",    h_u1_6 },
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++)
        if (strcmp(T[i].dll, dll) == 0 && strcmp(T[i].name, name) == 0) return T[i].fn;
    return NULL;
}
