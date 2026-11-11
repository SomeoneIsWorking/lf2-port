/* WinMain's frame loop: guest messages every turn, the game frame when its time comes on the guest
 * clock, the guest's frame counter, and a guest sleep when nothing is due. A logic frame is every
 * turn up to and including one game frame, so running one again replays its messages and waits. */
#ifndef LF2_MAIN_LOOP_H
#define LF2_MAIN_LOOP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One game frame, as the loop and the startup sequence run it. */
void main_loop_run_game_frame(void);
void main_loop_start(void);
/* The turns of the next logic frame. 0 if a quit came first, and no frame ran. */
int main_loop_advance(void);

/* What a snapshot keeps of the loop. */
typedef struct {
    uint32_t previous_tick;
} MainLoopState;
MainLoopState main_loop_state(void);
void main_loop_restore(MainLoopState state);

#ifdef __cplusplus
}
#endif

#endif
