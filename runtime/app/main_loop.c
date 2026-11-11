#include "main_loop.h"

#include "guest.h"
#include "guest_calls.h"
#include "hostwin.h"

enum {
    IAT_SLEEP = 0x00447098,
    FRAME_COUNTER = 0x00458580,
    FRAME_ARGUMENT = 0x00451dac,
    FAST_PACING = 0x0044d02c,
};

static uint32_t previous_tick;

void main_loop_run_game_frame(void)
{
    const uint32_t args[] = {LD32(FRAME_ARGUMENT)};
    call_guest_cdecl(0x0043e9a0, args, 1, 0x0043d187);
    if ((int32_t)R(EAX) < 0) call_guest_no_args(0x0043e890, 0x0043d193);
}

static void pump_guest_messages(void)
{
    call_guest_no_args(0x0043d230, 0x0043d152);
}

static void guest_sleep(uint32_t milliseconds)
{
    const uint32_t args[] = {milliseconds};
    call_import(IAT_SLEEP, args, 1, 0, 0x0043d1ef);
}

static void advance_frame_counter(void)
{
    uint32_t frame = LD32(FRAME_COUNTER) + 1;
    if (frame >= 60) frame = 0;
    ST32(FRAME_COUNTER, frame);
}

void main_loop_start(void)
{
    previous_tick = guest_time_ms();
}

int main_loop_advance(void)
{
    for (;;) {
        pump_guest_messages();
        if (hostwin_quit_requested()) return 0;

        /* >= not >: the quantised guest clock can rest exactly on the period. */
        const uint32_t interval = LD32(FAST_PACING) ? 33 : 3;
        uint32_t now = guest_time_ms();
        const uint32_t elapsed = now - previous_tick;
        int ran = 0;
        if (elapsed >= interval) {
            if (elapsed > 100) previous_tick = now - 100;
            main_loop_run_game_frame();
            previous_tick += interval;
            ran = 1;
        }

        now = guest_time_ms();
        const int32_t remaining = (int32_t)(previous_tick - now + interval);
        if (remaining > 0) guest_sleep((uint32_t)(remaining > 5 ? 5 : remaining));
        advance_frame_counter();
        if (ran) return 1;
    }
}

MainLoopState main_loop_state(void)
{
    return (MainLoopState){.previous_tick = previous_tick};
}

void main_loop_restore(MainLoopState state)
{
    previous_tick = state.previous_tick;
}
