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

/* ---- window ---- */

static void h_RegisterClassA(void)
{
    /* Keep the WNDPROC: the game drives part of its state from window messages. */
    hw.wndproc = LD32(ARG(0) + 4);
    ret_stdcall(1, 1);
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
    hw.hwnd = 0x00010000;
    ret_stdcall(12, hw.hwnd);
}

static void h_GetClientRect(void)
{
    uint32_t r = ARG(1);
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
        if (e.type == SDL_EVENT_QUIT) quit_posted = 1;
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE &&
            (e.key.mod & SDL_KMOD_SHIFT)) quit_posted = 1;
    }
}

static void fill_msg(uint32_t p, uint32_t msg)
{
    ST32(p, hw.hwnd); ST32(p + 4, msg);
    ST32(p + 8, 0); ST32(p + 12, 0);
    ST32(p + 16, 0); ST32(p + 20, 0); ST32(p + 24, 0);
}

enum { WM_QUIT = 0x0012 };

static void h_PeekMessageA(void)
{
    hostwin_pump();
    if (quit_posted) { fill_msg(ARG(0), WM_QUIT); ret_stdcall(5, 1); return; }
    ret_stdcall(5, 0);
}

static void h_GetMessageA(void)
{
    hostwin_pump();
    if (quit_posted) { fill_msg(ARG(0), WM_QUIT); ret_stdcall(4, 0); return; }
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

static void h_GetKeyState(void)
{
    hostwin_pump();
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
static void h_ClientToScreen(void) { ret_stdcall(2, 1); }

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




/* ---- GDI ----
 * DirectDraw's GetDC hands back the surface object itself, so a "DC" here is either a
 * surface or one of these memory DCs. Text rendering is not implemented yet: TextOutA
 * is a visible gap, logged once, not silently dropped. */
static uint32_t gdi_bk, gdi_fg;

static void h_CreateCompatibleDC(void) { ret_stdcall(1, 0xD0000001u); }
static void h_DeleteDC(void)     { ret_stdcall(1, 1); }
static void h_DeleteObject(void) { ret_stdcall(1, 1); }
static void h_SelectObject(void) { ret_stdcall(2, 0); }
static void h_SetBkColor(void)   { gdi_bk = ARG(1); ret_stdcall(2, 0); }
static void h_SetTextColor(void) { gdi_fg = ARG(1); ret_stdcall(2, 0); }

static void h_GetObjectA(void)
{
    /* BITMAP: bmType, bmWidth, bmHeight, bmWidthBytes, bmPlanes, bmBitsPixel, bmBits */
    const uint32_t out = ARG(2);
    if (out && ARG(1) >= 24) {
        ST32(out, 0);
        ST32(out + 4, (uint32_t)hw.width);
        ST32(out + 8, (uint32_t)hw.height);
        ST32(out + 12, (uint32_t)((hw.width + 3) & ~3));
        ST32(out + 16, 1);
        ST32(out + 20, 8);
    }
    ret_stdcall(3, 24);
}

static void h_TextOutA(void)
{
    static int warned;
    if (!warned) {
        fprintf(stderr, "note: GDI TextOutA not implemented -- some text will be missing\n");
        warned = 1;
    }
    ret_stdcall(5, 1);
}

static void h_StretchBlt(void) { ret_stdcall(11, 1); }

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
        { "USER32.dll", "LoadImageA",        h_u1_6 },
        { "USER32.dll", "SetCursor",         h_u1_1 },
        { "ole32.dll",  "CoInitialize",      h_CoInitialize },
        { "ole32.dll",  "CoCreateInstance",  h_CoCreateInstance },
        { "SHELL32.dll", "ShellExecuteA",    h_u1_6 },
        { "GDI32.dll", "CreateCompatibleDC", h_CreateCompatibleDC },
        { "GDI32.dll", "DeleteDC",           h_DeleteDC },
        { "GDI32.dll", "DeleteObject",       h_DeleteObject },
        { "GDI32.dll", "SelectObject",       h_SelectObject },
        { "GDI32.dll", "GetObjectA",         h_GetObjectA },
        { "GDI32.dll", "SetBkColor",         h_SetBkColor },
        { "GDI32.dll", "SetTextColor",       h_SetTextColor },
        { "GDI32.dll", "TextOutA",           h_TextOutA },
        { "GDI32.dll", "StretchBlt",         h_StretchBlt },
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++)
        if (strcmp(T[i].dll, dll) == 0 && strcmp(T[i].name, name) == 0) return T[i].fn;
    return NULL;
}
