/* USER32 / GDI32 / ole32 / shell, on SDL3. */
#include "com.h"
#include "guest_ops.h"
#include "hostwin.h"

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
void gamepad_handle_event(const SDL_Event *e);

/* ---- window ---- */

static void h_RegisterClassA(void)
{
    /* Keep the WNDPROC: the game drives part of its state from window messages. */
    hw.wndproc = LD32(ARG(0) + 4);
    ret_stdcall(1, 1);
}

/* Window mode. The game only ever asks for a fixed-size bordered window, so the choice
 * lives here rather than being something it can express. Letterboxed logical presentation
 * means the game still renders at its native 794x550 whatever the window becomes. */
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
    hw.width  = (int)ARG(6);
    hw.height = (int)ARG(7);
    if (hw.width <= 0 || hw.width > 4096) hw.width = 794;
    if (hw.height <= 0 || hw.height > 4096) hw.height = 550;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        abort();
    }
    hw.window = SDL_CreateWindow("Little Fighter 2", hw.width, hw.height,
                                 SDL_WINDOW_RESIZABLE);
    if (!hw.window) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); abort(); }
    hw.renderer = SDL_CreateRenderer(hw.window, NULL);
    SDL_SetRenderLogicalPresentation(hw.renderer, hw.width, hw.height,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
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

void hostwin_pump(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        gamepad_handle_event(&e);          /* controllers may come and go at any time */
        if (e.type == SDL_EVENT_QUIT) quit_posted = 1;
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE &&
            (e.key.mod & SDL_KMOD_SHIFT)) quit_posted = 1;
        /* Alt+Enter is what players expect, and the game cannot ask for it itself. */
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_RETURN &&
            (e.key.mod & SDL_KMOD_ALT)) toggle_fullscreen();
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

static int next_startup_message(uint32_t p)
{
    if (startup_head >= startup_count) return 0;
    ST32(p, hw.hwnd);
    ST32(p + 4, startup_queue[startup_head].msg);
    ST32(p + 8, startup_queue[startup_head].wparam);
    ST32(p + 12, startup_queue[startup_head].lparam);
    ST32(p + 16, 0); ST32(p + 20, 0); ST32(p + 24, 0);
    startup_head++;
    return 1;
}

static void h_PeekMessageA(void)
{
    hostwin_pump();
    if (quit_posted) { fill_msg(ARG(0), WM_QUIT); ret_stdcall(5, 1); return; }
    if (next_startup_message(ARG(0))) { ret_stdcall(5, 1); return; }
    ret_stdcall(5, 0);
}

static void h_GetMessageA(void)
{
    hostwin_pump();
    if (quit_posted) { fill_msg(ARG(0), WM_QUIT); ret_stdcall(4, 0); return; }
    if (next_startup_message(ARG(0))) { ret_stdcall(4, 1); return; }
    fill_msg(ARG(0), 0);
    ret_stdcall(4, 1);
}

static void h_DispatchMessageA(void)
{
    const uint32_t p = ARG(0);
    const uint32_t msg = LD32(p + 4);
    if (msg && hw.wndproc) {
        const uint32_t args[4] = { LD32(p), msg, LD32(p + 8), LD32(p + 12) };
        guest_call(hw.wndproc, args, 4);
    }
    ret_stdcall(1, 0);
}

/* ---- keyboard ----
 * Virtual-key codes the game polls, mapped to SDL scancodes. */
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

/* Scripted input, for verifying the port without a human at the keyboard.
 *
 *   LF2_AUTOKEY=<vk>[,<vk>...]   press each virtual key in turn
 *   LF2_AUTOKEY_START=<ms>       when to begin (default 6000)
 *   LF2_AUTOKEY_EVERY=<ms>       gap between presses (default 2500)
 *
 * Each key is reported held for 120 ms, which is long enough for a frame-polled menu to
 * see it and short enough not to auto-repeat. */
static int autokey_pressed(uint32_t vk)
{
    const char *script = getenv("LF2_AUTOKEY");
    if (!script) return 0;

    static uint64_t start_ms;
    if (!start_ms) start_ms = SDL_GetTicks();
    const char *s_env = getenv("LF2_AUTOKEY_START");
    const char *e_env = getenv("LF2_AUTOKEY_EVERY");
    const uint64_t begin = s_env ? (uint64_t)strtoul(s_env, NULL, 10) : 6000;
    const uint64_t every = e_env ? (uint64_t)strtoul(e_env, NULL, 10) : 2500;

    const uint64_t now = SDL_GetTicks() - start_ms;
    if (now < begin) return 0;
    const uint64_t elapsed = now - begin;
    const uint64_t index = elapsed / every;
    if (elapsed % every > 120) return 0;              /* held briefly */

    unsigned n = 0;
    for (const char *c = script; *c; ) {
        const uint32_t key = (uint32_t)strtoul(c, (char **)&c, 16);
        if (n == index % 64 && key == vk) return 1;
        n++;
        while (*c == ',' || *c == ' ') c++;
        if (n > 64) break;
    }
    return 0;
}

static void h_GetKeyState(void)
{
    hostwin_pump();
    if (autokey_pressed(ARG(0))) { ret_stdcall(1, 0xFF80u); return; }
    const SDL_Scancode sc = vk_to_scancode(ARG(0));
    int n = 0;
    const bool *state = SDL_GetKeyboardState(&n);
    const int down = (sc != SDL_SCANCODE_UNKNOWN && sc < n && state[sc]);
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

static void h_u0_1(void) { ret_stdcall(1, 0); }
static void h_u1_1(void) { ret_stdcall(1, 1); }
static void h_u1_2(void) { ret_stdcall(2, 1); }
static void h_u1_3(void) { ret_stdcall(3, 1); }
static void h_u1_6(void) { ret_stdcall(6, 1); }
static void h_u1_0(void) { ret_stdcall(0, 1); }
static void h_u1_4_defwnd(void) { ret_stdcall(4, 0); }

/* ole32: the game only uses COM to reach DirectSound. */
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
        { "ole32.dll",  "CoInitialize",      h_CoInitialize },
        { "ole32.dll",  "CoCreateInstance",  h_CoCreateInstance },
        { "SHELL32.dll", "ShellExecuteA",    h_u1_6 },
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++)
        if (strcmp(T[i].dll, dll) == 0 && strcmp(T[i].name, name) == 0) return T[i].fn;
    return NULL;
}
