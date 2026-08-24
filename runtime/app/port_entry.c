#include "port_entry.h"

#include "boot_guest.h"
#include "guest_ops.h"
#include "hostwin.h"
#include "startup_init.h"

#include <stdint.h>
#include <stdio.h>

void fn_004014e0(void);
void fn_00401970(void);
void fn_00402020(void);
void fn_004031b0(void);
void fn_00419e40(void);
void fn_0043bec0(void);
void fn_0043bf10(void);
void fn_0043c4a0(void);
void fn_0043c690(void);
void fn_0043c780(void);
void fn_0043cc60(void);
void fn_0043d230(void);
void fn_0043e890(void);
void fn_0043e9a0(void);
void fn_00414440(void);

enum {
    IAT_SLEEP = 0x00447098,
    IAT_INITIALIZE_CRITICAL_SECTION = 0x004470b4,
    IAT_SRAND = 0x004470ec,
    IAT_SPRINTF = 0x00447174,
    IAT_TIME64 = 0x004471ac,
    IAT_LOCALTIME64 = 0x004471a8,
    IAT_LOAD_CURSOR = 0x004471dc,
    IAT_SET_CURSOR = 0x004471f8,
    IAT_TIME_GET_TIME = 0x00447250,
    IAT_CO_INITIALIZE = 0x004472b4,

    AD_CRITICAL_SECTION = 0x004554a4,
    FRAME_SURFACE = 0x00455608,
    WINDOW_HANDLE = 0x004546f4,
    WORLD = 0x00458b00,
    FRAME_COUNTER = 0x00458580,
    FRAME_ARGUMENT = 0x00451dac,
    FAST_PACING = 0x0044d02c,
    UPDATE_DAYS = 0x0044d788,
    BUILD_DATE = 0x00451d48,
    UPDATE_DATE = 0x00458350,
    DATE_FORMAT = 0x0044a014,
    KEY_STATE = 0x00455378,
    INITIAL_MUSIC = 0x00447744,
};

typedef void (*GuestFunction)(void);

static void push_arguments(const uint32_t *args, unsigned count)
{
    while (count) PUSH32(args[--count]);
}

static uint32_t call_guest_cdecl(GuestFunction function, const uint32_t *args, unsigned count, uint32_t return_address)
{
    push_arguments(args, count);
    PUSH32(return_address);
    function();
    R(ESP) += 4 * count;
    return R(EAX);
}

static uint32_t call_import(uint32_t iat, const uint32_t *args, unsigned count, int caller_cleans,
                            uint32_t return_address)
{
    push_arguments(args, count);
    PUSH32(return_address);
    dispatch(LD32(iat));
    if (caller_cleans) R(ESP) += 4 * count;
    return R(EAX);
}

static uint32_t call_guest_no_args(GuestFunction function, uint32_t return_address)
{ return call_guest_cdecl(function, NULL, 0, return_address); }

static void construct_guest_globals(void)
{
    /* These are the three actual game constructors from the PE's initializer table. The
     * surrounding MSVC CRT startup is process scaffolding, not game state, and is no longer
     * part of the port's entry path. */
    R(ECX) = 0x00458440;
    call_guest_no_args(fn_004031b0, 0x004462e0);
    R(ECX) = 0x00458af8;
    call_guest_no_args(fn_00414440, 0x004462f0);
    R(ECX) = WORLD;
    call_guest_no_args(fn_00419e40, 0x00446300);
}

static uint32_t guest_time_ms(void) { return call_import(IAT_TIME_GET_TIME, NULL, 0, 0, 0x0043d162); }

static void guest_sleep(uint32_t milliseconds)
{
    const uint32_t args[] = {milliseconds};
    call_import(IAT_SLEEP, args, 1, 0, 0x0043d1ef);
}

static void format_date(uint32_t destination, uint32_t tm)
{
    const uint32_t args[] = {
        destination,  DATE_FORMAT,   LD32(tm),          LD32(tm + 4),
        LD32(tm + 8), LD32(tm + 12), LD32(tm + 16) + 1, LD32(tm + 20) + 1900,
    };
    call_import(IAT_SPRINTF, args, 8, 1, 0x0043d005);
}

static void initialise_dates(void)
{
    R(ESP) -= 8;
    const uint32_t time_value = R(ESP);
    const uint32_t no_destination[] = {0};
    call_import(IAT_TIME64, no_destination, 1, 1, 0x0043cfbe);
    ST32(time_value, R(EAX));
    ST32(time_value + 4, R(EDX));

    const uint32_t local_args[] = {time_value};
    uint32_t tm = call_import(IAT_LOCALTIME64, local_args, 1, 1, 0x0043cfd3);
    format_date(BUILD_DATE, tm);

    const int64_t now = (int64_t)(uint64_t)LD32(time_value) | ((int64_t)(uint64_t)LD32(time_value + 4) << 32);
    const int64_t update = now + (int64_t)(int32_t)LD32(UPDATE_DAYS) * 86400;
    ST32(time_value, (uint32_t)update);
    ST32(time_value + 4, (uint32_t)((uint64_t)update >> 32));
    tm = call_import(IAT_LOCALTIME64, local_args, 1, 1, 0x0043d028);
    format_date(UPDATE_DATE, tm);
    R(ESP) += 8;
}

static int initialise_window(void)
{
    ST32(0x00458420, 0);
    const uint32_t seed = guest_time_ms();
    const uint32_t seed_args[] = {seed};
    call_import(IAT_SRAND, seed_args, 1, 1, 0x0043cf60);

    const uint32_t critical_args[] = {AD_CRITICAL_SECTION};
    call_import(IAT_INITIALIZE_CRITICAL_SECTION, critical_args, 1, 0, 0x0043cf6e);
    const uint32_t co_args[] = {0};
    call_import(IAT_CO_INITIALIZE, co_args, 1, 0, 0x0043cf76);

    const uint32_t window_args[] = {0x00400000, 10};
    return call_guest_cdecl(fn_0043bec0, window_args, 2, 0x0043cf85) != 0;
}

static void initialise_ad_tables(void)
{
    if (!call_guest_no_args(fn_0043c4a0, 0x0043cf99) || !call_guest_no_args(fn_0043c780, 0x0043cfa2) ||
        !call_guest_no_args(fn_0043cc60, 0x0043cfab))
        call_guest_no_args(fn_0043c690, 0x0043cfb4);
}

static void initialise_input_and_sound(void)
{
    const uint32_t cursor_args[] = {0, 0x7f00};
    const uint32_t cursor = call_import(IAT_LOAD_CURSOR, cursor_args, 2, 0, 0x0043d071);
    const uint32_t set_cursor_args[] = {cursor};
    call_import(IAT_SET_CURSOR, set_cursor_args, 1, 0, 0x0043d078);

    for (uint32_t i = 0; i < 0x100; ++i) ST8(KEY_STATE + i, 0x75);
    call_guest_no_args(fn_0043bf10, 0x0043d08e);

    const uint32_t sound_args[] = {LD32(WINDOW_HANDLE)};
    if (!call_guest_cdecl(fn_00401970, sound_args, 1, 0x0043d099))
        fprintf(stderr, "startup: DirectSound initialization failed; continuing without audio\n");
}

static void construct_global_sounds(void)
{
    static const struct {
        uint32_t object;
        uint32_t path;
        uint32_t return_address;
    } sounds[] = {
        {0x0045560c, 0x0044a12c, 0x0043d0c4}, {0x00455610, 0x0044a11c, 0x0043d0d3},
        {0x00455614, 0x0044a108, 0x0043d0e2}, {0x00455618, 0x0044a0f8, 0x0043d0f1},
        {0x0045561c, 0x0044a0e8, 0x0043d100},
    };
    for (unsigned i = 0; i < sizeof sounds / sizeof sounds[0]; ++i) {
        const uint32_t args[] = {sounds[i].path};
        R(ECX) = sounds[i].object;
        push_arguments(args, 1);
        PUSH32(sounds[i].return_address);
        fn_004014e0();
    }
}

static void run_game_frame(void)
{
    const uint32_t args[] = {LD32(FRAME_ARGUMENT)};
    call_guest_cdecl(fn_0043e9a0, args, 1, 0x0043d187);
    if ((int32_t)R(EAX) < 0) call_guest_no_args(fn_0043e890, 0x0043d193);
}

static void pump_guest_messages(void) { call_guest_no_args(fn_0043d230, 0x0043d152); }

static void advance_frame_counter(void)
{
    uint32_t frame = LD32(FRAME_COUNTER) + 1;
    if (frame >= 60) frame = 0;
    ST32(FRAME_COUNTER, frame);
}

static int run_main_loop(uint32_t previous_tick)
{
    while (!hostwin_quit_requested()) {
        pump_guest_messages();
        if (hostwin_quit_requested()) break;

        const uint32_t interval = LD32(FAST_PACING) ? 33 : 3;
        uint32_t now = guest_time_ms();
        uint32_t elapsed = now - previous_tick;
        if (elapsed > interval) {
            if (elapsed > 100) previous_tick = now - 100;
            run_game_frame();
            previous_tick += interval;
        }

        now = guest_time_ms();
        const int32_t remaining = (int32_t)(previous_tick - now + interval);
        if (remaining > 0) guest_sleep((uint32_t)(remaining > 5 ? 5 : remaining));
        advance_frame_counter();
    }
    return 0;
}

int port_entry_run(void)
{
    fprintf(stderr, "startup: entering native port entry (guest PE entry and WinMain bypassed)\n");
    construct_guest_globals();
    if (!initialise_window()) {
        fprintf(stderr, "startup: DirectDraw/window initialization failed\n");
        return 1;
    }

    initialise_ad_tables();
    initialise_dates();
    initialise_input_and_sound();

    const uint32_t frame_surface = LD32(FRAME_SURFACE);
    if (!frame_surface) {
        fprintf(stderr, "startup: window initialization produced no frame surface\n");
        return 1;
    }

    /* This is the defining ordering of the custom entry: data construction is complete and
     * one real game frame is presented before the old WinMain's explicit music handoff. */
    fprintf(stderr, "startup: native data initialization begin\n");
    boot_guest_load_data(WORLD, frame_surface);
    fprintf(stderr, "startup: native data initialization complete\n");
    construct_global_sounds();
    run_game_frame();
    startup_init_ready();

    const uint32_t music_args[] = {INITIAL_MUSIC};
    call_guest_cdecl(fn_00402020, music_args, 1, 0x0043d061);
    return run_main_loop(guest_time_ms());
}
