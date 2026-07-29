/* Controllers, on SDL3's gamepad layer.
 *
 * The game drives the pre-2000 winmm joystick API: it probes ids 0 and 1 once at startup,
 * calls joySetCapture, and never looks again. That API has no concept of a device
 * arriving, which is why controllers plugged in after launch were invisible and why
 * swapping one mid-session did nothing. Nothing in the game needed changing to fix it --
 * the slots below are re-bound live from SDL's add/remove events, so the same
 * joyGetPosEx the game already calls simply starts reporting the new device.
 */
#include "guest_ops.h"
#include "hostwin.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define ARG(n) LD32(R(ESP) + 4 + 4 * (n))

enum { JOYERR_NOERROR = 0, JOYERR_UNPLUGGED = 167, JOY_SLOTS = 2 };

/* winmm reports axes over this range; the game scales against the min/max it is told. */
enum { AXIS_MIN = 0, AXIS_MAX = 65535, AXIS_CENTRE = 32768 };

static SDL_Gamepad *slot[JOY_SLOTS];
static SDL_JoystickID slot_id[JOY_SLOTS];
static int initialised;

static void ret_stdcall(int nargs, uint32_t value)
{
    R(EAX) = value;
    R(ESP) += 4 + 4u * (unsigned)nargs;
}

/* Fill any free slot from the currently attached devices. Called at startup and again on
 * every add event, so a controller plugged in mid-game lands in the first free slot. */
static void bind_available(void)
{
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (!ids) return;

    for (int i = 0; i < count; i++) {
        int already = 0;
        for (int sl = 0; sl < JOY_SLOTS; sl++)
            if (slot[sl] && slot_id[sl] == ids[i]) already = 1;
        if (already) continue;

        for (int sl = 0; sl < JOY_SLOTS; sl++) {
            if (slot[sl]) continue;
            SDL_Gamepad *pad = SDL_OpenGamepad(ids[i]);
            if (!pad) break;
            slot[sl] = pad;
            slot_id[sl] = ids[i];
            fprintf(stderr, "controller %d connected: %s\n", sl,
                    SDL_GetGamepadName(pad) ? SDL_GetGamepadName(pad) : "unnamed");
            break;
        }
    }
    SDL_free(ids);
}

static void unbind(SDL_JoystickID id)
{
    for (int sl = 0; sl < JOY_SLOTS; sl++) {
        if (!slot[sl] || slot_id[sl] != id) continue;
        fprintf(stderr, "controller %d disconnected\n", sl);
        SDL_CloseGamepad(slot[sl]);
        slot[sl] = NULL;
        slot_id[sl] = 0;
    }
}

/* Called from the message pump, so hotplug is noticed without the game asking. */
void gamepad_handle_event(const SDL_Event *e)
{
    if (e->type == SDL_EVENT_GAMEPAD_ADDED) bind_available();
    else if (e->type == SDL_EVENT_GAMEPAD_REMOVED) unbind(e->gdevice.which);
}

static void ensure_init(void)
{
    if (initialised) return;
    initialised = 1;
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "gamepad subsystem unavailable: %s\n", SDL_GetError());
        return;
    }
    bind_available();
}

static SDL_Gamepad *pad_for(uint32_t id)
{
    ensure_init();
    return (id < JOY_SLOTS) ? slot[id] : NULL;
}

/* ---- the winmm entry points ---- */

static void h_joyGetNumDevs(void)
{
    ensure_init();
    ret_stdcall(0, JOY_SLOTS);
}

static void h_joyGetDevCaps(void)
{
    const uint32_t id = ARG(0), caps = ARG(1), size = ARG(2);
    SDL_Gamepad *pad = pad_for(id);
    if (!pad || !caps) { ret_stdcall(3, JOYERR_UNPLUGGED); return; }

    for (uint32_t i = 0; i < size && i < 404; i += 4) ST32(caps + i, 0);

    ST16(caps + 0, 0x045E);                  /* wMid  */
    ST16(caps + 2, 0x028E);                  /* wPid  */
    const char *name = SDL_GetGamepadName(pad);
    snprintf((char *)(g_mem + caps + 4), 32, "%s", name ? name : "Gamepad");

    ST32(caps + 36, AXIS_MIN); ST32(caps + 40, AXIS_MAX);   /* X */
    ST32(caps + 44, AXIS_MIN); ST32(caps + 48, AXIS_MAX);   /* Y */
    ST32(caps + 52, AXIS_MIN); ST32(caps + 56, AXIS_MAX);   /* Z */
    ST32(caps + 60, 10);                     /* wNumButtons */
    ST32(caps + 64, 10);                     /* wPeriodMin  */
    ST32(caps + 68, 1000);                   /* wPeriodMax  */
    ST32(caps + 96, 0x0001);                 /* JOYCAPS_HASZ */
    ST32(caps + 100, 3);                     /* wMaxAxes    */
    ST32(caps + 104, 3);                     /* wNumAxes    */
    ST32(caps + 108, 10);                    /* wMaxButtons */
    ret_stdcall(3, JOYERR_NOERROR);
}

/* Both the left stick and the d-pad drive the axes: the game only understands axes, and a
 * fighting game is played on the d-pad. */
static uint32_t axis_value(SDL_Gamepad *pad, SDL_GamepadAxis axis,
                           SDL_GamepadButton negative, SDL_GamepadButton positive)
{
    if (SDL_GetGamepadButton(pad, negative)) return AXIS_MIN;
    if (SDL_GetGamepadButton(pad, positive)) return AXIS_MAX;

    const int raw = SDL_GetGamepadAxis(pad, axis);          /* -32768..32767 */
    if (raw > -8000 && raw < 8000) return AXIS_CENTRE;      /* dead zone */
    return (uint32_t)((raw + 32768) & 0xffff);
}

static void h_joyGetPosEx(void)
{
    const uint32_t id = ARG(0), info = ARG(1);
    SDL_Gamepad *pad = pad_for(id);
    if (!pad || !info) { ret_stdcall(2, JOYERR_UNPLUGGED); return; }

    static const SDL_GamepadButton BUTTONS[] = {
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST,
        SDL_GAMEPAD_BUTTON_WEST,  SDL_GAMEPAD_BUTTON_NORTH,
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
        SDL_GAMEPAD_BUTTON_BACK,  SDL_GAMEPAD_BUTTON_START,
        SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    };
    uint32_t buttons = 0;
    for (unsigned i = 0; i < sizeof BUTTONS / sizeof BUTTONS[0]; i++)
        if (SDL_GetGamepadButton(pad, BUTTONS[i])) buttons |= 1u << i;

    ST32(info + 8,  axis_value(pad, SDL_GAMEPAD_AXIS_LEFTX,
                               SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
    ST32(info + 12, axis_value(pad, SDL_GAMEPAD_AXIS_LEFTY,
                               SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN));
    ST32(info + 16, AXIS_CENTRE);                    /* Z */
    ST32(info + 32, buttons);
    ST32(info + 40, 0xFFFF);                         /* dwPOV: centred */
    ret_stdcall(2, JOYERR_NOERROR);
}

static void h_joyGetPos(void)
{
    const uint32_t id = ARG(0), info = ARG(1);
    SDL_Gamepad *pad = pad_for(id);
    if (!pad || !info) { ret_stdcall(2, JOYERR_UNPLUGGED); return; }
    ST32(info + 0, axis_value(pad, SDL_GAMEPAD_AXIS_LEFTX,
                              SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
    ST32(info + 4, axis_value(pad, SDL_GAMEPAD_AXIS_LEFTY,
                              SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN));
    ST32(info + 8, AXIS_CENTRE);
    ST32(info + 12, 0);
    ret_stdcall(2, JOYERR_NOERROR);
}

/* The game asks Windows to post MM_JOY messages on a timer. Nothing needs to happen: it
 * polls with joyGetPosEx anyway, and the polled state is always current. */
static void h_joySetCapture(void)   { ret_stdcall(4, JOYERR_NOERROR); }
static void h_joySetThreshold(void) { ret_stdcall(2, JOYERR_NOERROR); }
static void h_joyReleaseCapture(void) { ret_stdcall(1, JOYERR_NOERROR); }

typedef void (*Handler)(void);

Handler gamepad_lookup(const char *dll, const char *name)
{
    static const struct { const char *name; Handler fn; } T[] = {
        { "joyGetNumDevs",     h_joyGetNumDevs },
        { "joyGetDevCapsA",    h_joyGetDevCaps },
        { "joyGetDevCapsW",    h_joyGetDevCaps },
        { "joyGetPosEx",       h_joyGetPosEx },
        { "joyGetPos",         h_joyGetPos },
        { "joySetCapture",     h_joySetCapture },
        { "joySetThreshold",   h_joySetThreshold },
        { "joyReleaseCapture", h_joyReleaseCapture },
    };
    if (strcmp(dll, "WINMM.dll") != 0) return NULL;
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++)
        if (strcmp(T[i].name, name) == 0) return T[i].fn;
    return NULL;
}
