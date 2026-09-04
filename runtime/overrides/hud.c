/* fn_0041ae60 -- the in-match HUD strip along the top of the screen, one panel per player
 * slot, and the one pass in the game that may see a slot the stage may not.
 *
 * One of the hand-written native replacements for guest routines; see
 * runtime/overrides/overrides.h for how the set is divided and why.
 *
 * WHAT THE GAME'S OWN PASS DOES, read off fn_0041ae60 rather than assumed. It loops the
 * eight player slots, draws the empty box for each unconditionally, and then:
 *
 *     if (this[4 + i])          draw the panel from this+404+4i          // a human
 *     else if (this[0xe + i])   draw the panel from this+404+4(i+10)     // a computer
 *     else                      leave the box empty
 *
 * -- the portrait through obj->0x368->0x728, the two bars through fn_0043f310 from obj->0x2fc
 * and obj->0x300, the name plate switching on obj->0x364. `this` is 0x00458b00, so `this+4`
 * is the port's EXISTS: THE SAME BYTE the stage pass (fn_0041a5a0, which collects every one
 * of the 400 indices whose byte is set and depth-sorts them) and the world step
 * (fn_004064d0) read.
 *
 * WHY THAT MATTERS, and why this file exists. A player who drops into a running match has to
 * choose a character before it plays, and the choice has to be visible somewhere. With one
 * byte serving all three passes there are only two states on offer: byte down, which is no
 * panel to choose in, and byte up, which puts a body on the stage in the middle of a fight.
 * The second is what shipped first and is issue #19.
 *
 * The two passes have to disagree, and the only place they can is between them. So: raise
 * the byte for a slot that is still choosing, call the game's own panel drawing, put it back
 * down. The stage pass and the world step run outside that window and never see the slot.
 * The panel that appears is the game's -- its portrait, its bars, its name plate, drawn by
 * its code off the record coop.c built -- so the port is not painting a picture of a
 * character-select screen, it is showing the one the game would draw.
 *
 * HOW THIS COULD BE WRONG, since it is a window rather than a flag: anything that reads
 * EXISTS from inside fn_0041ae60 would see the slot as live. The pass draws and does nothing
 * else -- no world state is written in it -- which is what makes the window safe, and it is
 * the reason this hook is here rather than somewhere more convenient like the blit path.
 * Suppressing the fighter's pixels at the blit would leave it walking about invisibly,
 * colliding and taking damage; it would look fixed and would not be.
 */

#include "overrides.h"
#include "world.h"

#include "guest.h"
#include "jit_executor.h"
#include "lf2_log.h"

#include <stdio.h>

/* `this`, derived from the offset the port already located rather than written twice. */
enum { HUD_THIS = EXISTS - 4 };

void fn_0041ae60(void)
{
    const uint32_t self = R(ECX); /* __thiscall */
    int raised[PLAYER_SLOTS], n = 0;

    /* Both of the game's call sites pass the same object the rest of this port calls `self`
     * (ECX = EBX at 0x0041d765 and 0x00421a28). If that ever stops being true the slot
     * indices below would address something else entirely, so it is checked rather than
     * trusted -- and said once, loudly, instead of quietly drawing nothing. */
    if (self == (uint32_t)HUD_THIS) {
        for (int i = 0; i < PLAYER_SLOTS; i++) {
            if (!coop_hud_preview(i)) continue;
            if (LD8(EXISTS + (uint32_t)i)) continue; /* already in the world; not ours */
            ST8(EXISTS + (uint32_t)i, 1);
            raised[n++] = i;
        }
    } else {
        static int said;
        if (!said) {
            said = 1;
            lf2_log_writef(LF2_LOG_INFO, "hud",
                           "hud: the HUD pass was called with this=%08x, not the %08x this "
                           "port's player slots are indexed from, so a joiner's panel is "
                           "NOT being raised and a drop-in has nothing to choose in\n",
                           self, (uint32_t)HUD_THIS);
        }
    }

    lf2_jit_call_original(0x0041ae60);

    for (int k = 0; k < n; k++) ST8(EXISTS + (uint32_t)raised[k], 0);
}
