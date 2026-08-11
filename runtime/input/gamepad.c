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
#include "script.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define ARG(n) LD32(R(ESP) + 4 + 4 * (n))

enum { JOYERR_NOERROR = 0, JOYERR_UNPLUGGED = 167, JOY_SLOTS = 2 };

/* winmm reports axes over this range; the game scales against the min/max it is told. */
enum { AXIS_MIN = 0, AXIS_MAX = 65535, AXIS_CENTRE = 32768 };

static SDL_Gamepad *slot[JOY_SLOTS];
static SDL_JoystickID slot_id[JOY_SLOTS];

/* The scripted pads, declared here because bind_available has to tell them from the
 * tester's own hardware. Attached in virtual_pad_tick, far below. */
static SDL_JoystickID virtual_id[JOY_SLOTS];
static int initialised;

static void ret_stdcall(int nargs, uint32_t value)
{
    R(EAX) = value;
    R(ESP) += 4 + 4u * (unsigned)nargs;
}

/* Fill any free slot from the currently attached devices. Called at startup and again on
 * every add event, so a controller plugged in mid-game lands in the first free slot. */
/* A scripted run is a TEST, and a test must not share the machine with the tester's own
 * hardware. A physical pad plugged into this box claims slot 0, and the front-end menu is
 * driven from slot 0 only -- so a scripted route pressed into slot 1 while the menu waited
 * on an idle controller nobody was touching, and the run reached no screen at all.
 *
 * That is not hypothetical: it is what "controller 0 connected: Xbox One S Controller"
 * ahead of "virtual pad 0: attached as joystick 4" did to every route test on this machine
 * (issue #18), and it was very nearly written down as CPU contention instead.
 *
 * So when a script is configured, ONLY virtual pads bind. Announced, because a run that
 * silently ignored the hardware would be its own confusion later. */
static int scripted_run(void)
{
    return getenv("LF2_VIRTUAL_PAD") != NULL || getenv("LF2_VIRTUAL_PAD2") != NULL;
}

static int is_virtual(SDL_JoystickID id)
{
    for (int i = 0; i < JOY_SLOTS; i++) if (virtual_id[i] == id) return 1;
    return 0;
}

static void bind_available(void)
{
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    if (!ids) return;

    for (int i = 0; i < count; i++) {
        if (scripted_run() && !is_virtual(ids[i])) {
            static SDL_JoystickID said[8];
            static int nsaid;
            int seen = 0;
            for (int k = 0; k < nsaid; k++) if (said[k] == ids[i]) seen = 1;
            if (!seen && nsaid < 8) {
                said[nsaid++] = ids[i];
                SDL_Gamepad *g = SDL_OpenGamepad(ids[i]);
                fprintf(stderr, "gamepad: IGNORING physical controller \"%s\" -- this run is "
                                "scripted (LF2_VIRTUAL_PAD), so only virtual pads bind and "
                                "the script is not competing with hardware for slot 0\n",
                        g && SDL_GetGamepadName(g) ? SDL_GetGamepadName(g) : "unnamed");
                if (g) SDL_CloseGamepad(g);
            }
            continue;
        }
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

/* Player input, in the game's own terms: the seven buttons a fighter has, in the order the
 * game stores them (up, down, left, right, attack, jump, defend). Returns 0 when nothing is
 * plugged into that slot, so a caller can tell "no pad" from "pad with nothing pressed" --
 * the two are not the same and conflating them would silently disable the feature. */
int gamepad_player_buttons(int index, unsigned char out[7])
{
    ensure_init();
    if (index < 0 || index >= JOY_SLOTS || !slot[index]) return 0;
    SDL_Gamepad *pad = slot[index];

    static const struct { SDL_GamepadButton b; SDL_GamepadAxis ax; int dir; } B[7] = {
        { SDL_GAMEPAD_BUTTON_DPAD_UP,    SDL_GAMEPAD_AXIS_LEFTY,   -1 },
        { SDL_GAMEPAD_BUTTON_DPAD_DOWN,  SDL_GAMEPAD_AXIS_LEFTY,   +1 },
        { SDL_GAMEPAD_BUTTON_DPAD_LEFT,  SDL_GAMEPAD_AXIS_LEFTX,   -1 },
        { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_AXIS_LEFTX,   +1 },
        { SDL_GAMEPAD_BUTTON_SOUTH,      SDL_GAMEPAD_AXIS_INVALID,  0 },  /* attack */
        { SDL_GAMEPAD_BUTTON_EAST,       SDL_GAMEPAD_AXIS_INVALID,  0 },  /* jump   */
        { SDL_GAMEPAD_BUTTON_WEST,       SDL_GAMEPAD_AXIS_INVALID,  0 },  /* defend */
    };
    for (int i = 0; i < 7; i++) {
        int down = SDL_GetGamepadButton(pad, B[i].b);
        if (!down && B[i].ax != SDL_GAMEPAD_AXIS_INVALID) {
            const int raw = SDL_GetGamepadAxis(pad, B[i].ax);
            down = B[i].dir < 0 ? (raw < -16000) : (raw > 16000);
        }
        out[i] = (unsigned char)!!down;
    }
    return 1;
}

/* Start, on any attached pad. The pause menu needs it separately from the seven game
 * buttons, which do not include Start. */
int gamepad_start_held(void)
{
    return gamepad_start_index() >= 0;
}

/* WHICH pad is holding Start, not merely whether one is. The pause menu needs it because
 * drop-out is per player: the menu is one screen, but the player it drops out is whoever
 * opened it, and a menu that guessed would drop the wrong fighter out of the match. */
int gamepad_start_index(void)
{
    ensure_init();
    for (int i = 0; i < JOY_SLOTS; i++)
        if (slot[i] && SDL_GetGamepadButton(slot[i], SDL_GAMEPAD_BUTTON_START)) return i;
    return -1;
}

/* ---- driving the front-end menu from a controller ----
 *
 * Only the front-end menu needs anything here. Everything the game itself drives from the
 * player buttons -- mode select, character selection, the pre-fight overlay and the match
 * -- gets the pad through the ported input gather in runtime/overrides.c, which merges it
 * into the game's own button state. The front end is the exception because it is driven by
 * a mouse rather than by player buttons, so what a pad moves there is the selection index
 * of the ported menu.
 *
 * This used to synthesise player-1 keypresses for all of it. That worked, but it was the
 * wrong shape: it made a controller pretend to be a keyboard at the window boundary
 * instead of being an input device the game understands, it could only ever drive player
 * one, and it silently depended on player one still having the default key bindings.
 */
void gamepad_drive_ui(void)
{
    ensure_init();
    SDL_Gamepad *pad = slot[0];
    if (!pad) return;

    static uint8_t was[4];
    static const struct { SDL_GamepadButton b; SDL_GamepadAxis ax; int dir; int delta; }
    MAP[] = {
        { SDL_GAMEPAD_BUTTON_DPAD_UP,    SDL_GAMEPAD_AXIS_LEFTY,   -1, -1 },
        { SDL_GAMEPAD_BUTTON_DPAD_DOWN,  SDL_GAMEPAD_AXIS_LEFTY,   +1, +1 },
        { SDL_GAMEPAD_BUTTON_SOUTH,      SDL_GAMEPAD_AXIS_INVALID,  0,  0 },  /* confirm */
        { SDL_GAMEPAD_BUTTON_START,      SDL_GAMEPAD_AXIS_INVALID,  0,  0 },  /* confirm */
    };

    for (unsigned i = 0; i < sizeof MAP / sizeof MAP[0]; i++) {
        int down = SDL_GetGamepadButton(pad, MAP[i].b);
        if (!down && MAP[i].ax != SDL_GAMEPAD_AXIS_INVALID) {
            const int raw = SDL_GetGamepadAxis(pad, MAP[i].ax);
            down = MAP[i].dir < 0 ? (raw < -16000) : (raw > 16000);
        }
        if (down == was[i]) continue;              /* edge-triggered, like a keypress */
        was[i] = (uint8_t)down;
        if (!down) continue;

        if (MAP[i].delta) menu_move(MAP[i].delta);
        else              menu_confirm();
    }
}

/* ---- virtual controller, for testing without hardware ----
 *
 * LF2_VIRTUAL_PAD=<script> attaches a software gamepad through SDL and plays a button
 * script into it. This is the only way any of the controller support gets exercised here,
 * since there is no physical pad to plug in -- auto-detect, hotswap and the menu driving
 * above were otherwise all untested code.
 *
 * The script is button:frame pairs, e.g. "down:60,down:90,south:120", where the names are
 * SDL_GamepadButton short names and the frame is when to press (released 8 frames later).
 *
 * LF2_VIRTUAL_PAD2 attaches a second one. That exists because "a second controller is
 * player two" was a claim with nothing behind it -- the slot-assignment code looked right,
 * but only one pad had ever been attached.
 */
static SDL_Joystick *virtual_pad[JOY_SLOTS];

static int button_by_name(const char *name, size_t n)
{
    static const struct { const char *n; SDL_GamepadButton b; } NAMES[] = {
        { "up", SDL_GAMEPAD_BUTTON_DPAD_UP }, { "down", SDL_GAMEPAD_BUTTON_DPAD_DOWN },
        { "left", SDL_GAMEPAD_BUTTON_DPAD_LEFT }, { "right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT },
        { "south", SDL_GAMEPAD_BUTTON_SOUTH }, { "east", SDL_GAMEPAD_BUTTON_EAST },
        { "west", SDL_GAMEPAD_BUTTON_WEST }, { "north", SDL_GAMEPAD_BUTTON_NORTH },
        { "start", SDL_GAMEPAD_BUTTON_START },
    };
    for (unsigned i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
        if (strlen(NAMES[i].n) == n && strncmp(NAMES[i].n, name, n) == 0) return NAMES[i].b;
    return -1;
}

/* ---- when a scripted press happens ----
 *
 * The timing model, the screen signal and the exit report are NOT here: they are shared
 * with the keyboard and mouse scripts in runtime/app/script.c, because they were only ever
 * right for the pad and the other two had neither (issue #25). What stays here is the pad's
 * own syntax -- `<button>` and the button names -- and pressing the virtual device.
 */
static void play_script(int slot, SDL_Joystick *pad, const char *script, long frame)
{
    int idx = 0;
    for (const char *c = script; *c; ) {
        const char *name = c;
        while (*c && *c != ':' && *c != '@') c++;
        const int btn = button_by_name(name, (size_t)(c - name));
        const char *spec = c;
        if (*c == ':') { c++; spec = c; }
        while (*c && *c != ',' && *c != ' ') c++;
        char buf[64];
        size_t n = (size_t)(c - spec);
        if (n >= sizeof buf) n = sizeof buf - 1;
        memcpy(buf, spec, n); buf[n] = 0;
        while (*c == ',' || *c == ' ') c++;

        const int i = idx++;
        const int stream = slot == 0 ? SCRIPT_PAD0 : SCRIPT_PAD1;
        script_seen(stream, i);

        /* A button name this build does not know never presses anything. Recorded as its
         * own state rather than skipped, because a typo that silently does nothing is the
         * same failure as a press that missed its screen, and reads the same from outside. */
        if (btn < 0) { script_bad_item(stream, i); continue; }

        int un = 0;
        const long at = script_when(buf, &un);
        if (un) continue;                  /* its screen has not appeared YET -- not never */
        if (frame == at) {
            SDL_SetJoystickVirtualButton(pad, btn, true);
            script_fired(stream, i);
        } else if (frame == at + 8) {
            SDL_SetJoystickVirtualButton(pad, btn, false);
        }
    }
}

void virtual_pad_tick(long frame)
{
    static const char *const VARS[JOY_SLOTS] = { "LF2_VIRTUAL_PAD", "LF2_VIRTUAL_PAD2" };

    script_observe_screens(frame);

    for (int i = 0; i < JOY_SLOTS; i++) {
        const char *script = getenv(VARS[i]);
        if (!script) continue;

        if (!virtual_pad[i]) {
            SDL_VirtualJoystickDesc desc;
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.naxes = 6;
            desc.nbuttons = 15;
            desc.name = "lf2 virtual pad";
            virtual_id[i] = SDL_AttachVirtualJoystick(&desc);
            if (!virtual_id[i]) {
                fprintf(stderr, "virtual pad %d: attach failed: %s\n", i, SDL_GetError());
                continue;
            }
            virtual_pad[i] = SDL_OpenJoystick(virtual_id[i]);
            fprintf(stderr, "virtual pad %d: attached as joystick %u\n",
                    i, (unsigned)virtual_id[i]);
            continue;                /* let the add event land before pressing anything */
        }
        play_script(i, virtual_pad[i], script, frame);
    }
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
