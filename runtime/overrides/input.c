/* fn_00419a60 -- the per-frame gather that turns devices into a fighter's buttons.
 *
 * One of the hand-written native replacements for recompiled functions; see
 * runtime/overrides/overrides.h for how the set is divided and why.
 */

#include "overrides.h"
#include "world.h"

#include "guest_ops.h"
#include "guest_map.h"
#include "hostwin.h"
#include "bindings.h"
#include "rmlui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fn_00419a60__orig(void);

/* Set when a mouse click should read as the attack button for one gather; see
 * overrides.h. Defined here, where it is spent. */
int mouse_confirm_frames;

enum { NET_OR_RECORD = 0x00450b80 };   /* non-zero: the packed masks are being consumed */
enum { RECORDING = 0x0044f1af, MASK_MIRROR = 0x0044d040 };

/* Bit for each of the seven buttons in the packed mask, in button order. The game writes
 * these itself further down its own loop; a pad press has to appear in them too, or a
 * recording made with a controller would replay as a player standing still. */
static const uint8_t BTN_BIT[7] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02 };

static long in_frames, in_live, in_padded, in_presses;

void input_report(void)
{
    fprintf(stderr, "input: %ld gathers, %ld live player-slots, %ld of them with a pad, "
                    "%ld button presses merged\n",
            in_frames, in_live, in_padded, in_presses);
}

/* ---- devices, first come first served ----
 *
 * One keyboard layout (remappable now: config.h, issue #70) plus every connected
 * pad form a pool of DEVICES. Outside the game proper every device drives player one, so
 * anyone can work the menus. Inside it (top-level mode 2: character selection and the
 * match) the first device to press anything claims player one, the next player two, and
 * so on; a claim holds until the game returns to the front end. Joining stays the game's
 * own logic -- a claimed device's attack lands in its player's buttons, and that is what
 * fn_0041bc90 already treats as "join".
 *
 * The original gather still runs first for everything that is not a live device: slot
 * bookkeeping, recording playback (device selector -1) and the packed-mask plumbing. Its
 * BUTTONS for live slots are then overwritten, not merged -- the control.txt keyboard
 * layouts are exactly what "only one keyboard layout" removes. */
enum { MAX_DEV = 5 };                    /* the keyboard, then up to four pads */

static int dev_player[MAX_DEV] = { -1, -1, -1, -1, -1 };
static unsigned char dev_prev[MAX_DEV][7];

/* Which player the keyboard claimed, or -1. The mouse drives the same one, so the two
 * never disagree about who they are. */
int keyboard_player(void) { return dev_player[0]; }

/* Which player a device is driving, for the pause menu: drop-out is per player, and the
 * player it drops out is the one belonging to whoever opened the menu. */
int device_player(int dev)
{
    return dev >= 0 && dev < MAX_DEV ? dev_player[dev] : -1;
}

/* The reverse, for the HUD: which DEVICE is driving a player slot (issue #74). 0 is the
 * keyboard, 1..4 the pads; -1 when no device drives it -- a computer slot, or one nobody
 * claimed. */
int device_for_player(int slot)
{
    for (int d = 0; d < MAX_DEV; d++)
        if (dev_player[d] == slot) return d;
    return -1;
}

static int device_buttons(int dev, unsigned char out[7])
{
    /* This gather reads host devices directly rather than Win32 key messages. Mirror the UI
     * input block here so a visible global document cannot also drive the menu or fighter
     * underneath it. Presence is preserved; only the seven action states are withheld. */
    if (rmlui_active()) {
        memset(out, 0, 7);
        if (dev == 0) return 1;
        unsigned char ignored[7];
        return gamepad_player_buttons(dev - 1, ignored);
    }
    if (dev == 0) {
        /* up, down, left, right, attack, jump, defend -- the game's button order, read from
         * the settings file so a player can remap them (config.h, issue #70). */
        for (int b = 0; b < B_N; b++)
            out[b] = (unsigned char)(hostwin_key_held(binding_key_vk(b)) != 0);
        /* A mouse click on a ported menu reads as this device's attack, so the game does
         * its own dispatch, sound and screen change rather than the port simulating them. */
        if (mouse_confirm_frames > 0) { mouse_confirm_frames--; out[4] = 1; }
        return 1;                        /* a keyboard is always there */
    }
    return gamepad_player_buttons(dev - 1, out);
}

void fn_00419a60(void)
{
    const uint32_t self = R(ECX);              /* __thiscall */
    const uint32_t mask_buf = LD32(R(ESP) + 12);

    fn_00419a60__orig();                       /* slots, recording, masks, unchanged */

    const int want_mask   = LD32(NET_OR_RECORD) != 0;
    const int want_mirror = (int8_t)LD8(RECORDING) > 0;
    const int in_game     = game_top_mode() == MODE_IN_GAME;

    in_frames++;

    /* Assignments live only inside the game proper; leaving it (or never having entered)
     * clears them, so the next character selection is first come first served again. */
    if (!in_game) {
        for (int d = 0; d < MAX_DEV; d++) dev_player[d] = -1;
        /* And so does anything this port put into a match: leaving the game proper ends
         * the match those slots and that half-finished selection belonged to. The roster is
         * kept -- it is the data file's, not the match's. */
        coop_reset();
    }

    /* Device states this frame, claims on a fresh press. A device's FIRST press both
     * claims the next free player and lands in that player's buttons, so pressing attack
     * on the join screen claims and joins in one stroke. */
    unsigned char btn[MAX_DEV][7];
    int present[MAX_DEV];
    for (int d = 0; d < MAX_DEV; d++) {
        present[d] = device_buttons(d, btn[d]);
        if (!present[d]) {
            memset(dev_prev[d], 0, 7);
            /* DROP-OUT: the device is gone -- unplugged mid-match. Its fighter goes with
             * it, but only if THIS code put it there; a slot the game's own character
             * selection filled is not ours to empty (coop_leave refuses those out loud),
             * and a keyboard is never absent, so this is a pad being pulled. */
            if (in_game && dev_player[d] >= 0 && coop_owns(dev_player[d])) {
                coop_leave(self, dev_player[d], "its device was disconnected");
                dev_player[d] = -1;
            }
            continue;
        }
        if (in_game && dev_player[d] < 0) {
            int fresh = 0;
            for (int b = 0; b < 7; b++) fresh |= btn[d][b] && !dev_prev[d][b];
            if (fresh) {
                int used[PLAYER_SLOTS] = { 0 };
                for (int e = 0; e < MAX_DEV; e++)
                    if (dev_player[e] >= 0 && dev_player[e] < PLAYER_SLOTS)
                        used[dev_player[e]] = 1;
                for (int p = 0; p < PLAYER_SLOTS; p++)
                    if (!used[p]) { dev_player[d] = p; break; }

                /* DROP-IN: a device that claims a slot while a match is ALREADY running
                 * has no fighter waiting for it -- character selection is over -- so one is
                 * built for it here.
                 *
                 * The slot it takes is not simply the one the claim loop above picked. That
                 * loop only avoids slots another DEVICE holds, and the game's own roster has
                 * its own opinion: a slot the character-select screen filled with a computer
                 * carries a non-zero device selector, and that computer's fighter is already
                 * on the stage at a high index. Joining such a slot does not replace it --
                 * the match just gains a fighter. So a slot the roster considers empty
                 * (selector 0) is preferred, and taking a computer's is announced rather
                 * than done quietly.
                 *
                 * ON, with nothing to switch on. It was opt-in for as long as a late
                 * joiner had no way to pick a character; now it opens a choice, which is
                 * what the join always needed to be, and a feature nobody can find is not
                 * a feature. The LF2_COOP_* variables that remain are the diagnostics over
                 * it -- LF2_COOP_SPAWN, _JOIN, _TABLE and the rest -- not switches. */
                int p = dev_player[d];
                if (p >= 0 && !LD8(EXISTS + (uint32_t)p) && coop_match_running(self)) {
                    /* WHICH SLOT, and it is the lowest one free -- a second human is
                     * Player 2, not Player 5.
                     *
                     * This used to prefer a slot whose device SELECTOR was zero, on the
                     * reasoning that a non-zero selector means character selection put a
                     * computer there. The reasoning is right and the conclusion was wrong:
                     * that computer's fighter is at its own high table index, unbound by
                     * the gather, so the selector says nothing about whether slot 1 can
                     * hold a human. What it produced was a joiner sitting in slot 4 with
                     * Player 2's box empty beside it -- observed in play, and visible in
                     * scratch/logs/select3.log as "slot 1 is on the game's roster already
                     * (selector 2), taking empty slot 4 instead".
                     *
                     * What actually disqualifies a slot is a FIGHTER IN IT -- the gate byte
                     * -- or another device already holding it. Joining a slot the roster
                     * listed as a computer does not replace that computer; the match gains
                     * a fighter, which is what a drop-in is. */
                    int low = -1;
                    for (int q = 0; q < PLAYER_SLOTS; q++) {
                        if (LD8(EXISTS + (uint32_t)q)) continue;
                        int taken = 0;
                        for (int e = 0; e < MAX_DEV; e++) if (dev_player[e] == q) taken = 1;
                        if (!taken) { low = q; break; }
                    }
                    if (low >= 0 && low != p) {
                        /* Not necessarily LOWER than the claim loop's pick: that loop only
                         * skips slots another device holds, so it can land on one that has
                         * a fighter standing in it. This is the lowest slot that is
                         * genuinely empty, which is above it in exactly that case. */
                        fprintf(stderr, "coop: slot %d is the lowest with no fighter in it, "
                                        "so device %d takes it instead of slot %d\n",
                                low, d, p);
                        dev_player[d] = low;
                        p = low;
                    }
                    if ((int32_t)LD32(DEVSEL + 4u * (uint32_t)p) != 0)
                        fprintf(stderr, "coop: slot %d carries selector %d, so character "
                                        "selection listed a computer there -- its fighter "
                                        "is at its own index and stays; the match gains a "
                                        "fighter rather than swapping one\n",
                                p, (int32_t)LD32(DEVSEL + 4u * (uint32_t)p));
                    /* The joiner CHOOSES: a fighter appears flashing and its device
                     * cycles the game's roster with left/right and confirms with attack.
                     * See coop_select_begin.
                     *
                     * No early exit here: this sits inside the per-device loop, and leaving
                     * it would skip the remaining devices' button bookkeeping for the
                     * frame. */
                    fprintf(stderr, "coop: device %d claimed player slot %d mid-match, "
                                    "opening its character selection\n", d, p);
                    coop_select_begin(self, p, d);
                }
            }
        }

        /* A device whose player is still choosing drives the choice, not the fighter. This
         * runs BEFORE dev_prev is updated, because every press in there is an edge. On the
         * frame the slot was claimed the tick returns without doing anything -- the press
         * that opened the selection must not also close it. */
        if (in_game && dev_player[d] >= 0 && coop_selecting(dev_player[d]))
            coop_select_tick(self, dev_player[d], btn[d], dev_prev[d]);

        memcpy(dev_prev[d], btn[d], 7);
    }

    coop_debug_tick(self);

    /* All EIGHT player slots, not four. The four was a port limitation, and it is removed
     * on evidence rather than optimism: the device-selector table is exactly eight entries
     * (DEVSEL..DEVSEL_END is 32 bytes), and a fighter put into slot 4 with its selector and
     * joined-mask bit set is drawn by the game's OWN name plate as "5". A slot with no
     * device bound to it contributes nothing here, because `fed` stays 0 and the loop
     * writes an all-zero button set exactly as it did before -- which is what the slots
     * beyond the first two in tools/routes/controller_2p_test.sh have always been. */
    for (uint32_t sel = DEVSEL, i = 0; sel < DEVSEL_END && i < PLAYER_SLOTS; sel += 4, i++) {
        if ((int32_t)LD32(sel) <= 0) continue;         /* recording or demo: not ours */
        in_live++;

        /* This slot's buttons from OUR devices only: outside the game everything routes
         * to the first slot; inside it, whatever claimed this player. A slot the game
         * has filled with a computer is still a live slot and its AI writes its buttons
         * after this gather, so writing here is harmless -- measured back when pads
         * merged by slot order (see tools/routes/controller_2p_test.sh). */
        unsigned char out[7] = { 0, 0, 0, 0, 0, 0, 0 };
        int fed = 0;
        for (int d = 0; d < MAX_DEV; d++) {
            if (!present[d]) continue;
            const int target = in_game ? dev_player[d] : 0;
            if (target != (int)i) continue;
            fed = 1;
            for (int b = 0; b < 7; b++) out[b] |= btn[d][b];
        }
        if (fed) in_padded++;

        /* A slot still choosing its character gets NO buttons. Its device's left and right
         * are cycling the roster and its attack is the confirm, and a fighter that also
         * walked and punched while its player chose would be acting on both readings of the
         * same press. Zeroed rather than skipped, so the record does not keep the last
         * buttons it was given. */
        if (in_game && coop_selecting(i)) memset(out, 0, sizeof out);

        const uint32_t obj = LD32(self + PLAYER_PTRS + 4 * i);
        if (!obj) continue;

        uint8_t mask = 0;
        for (int b = 0; b < 7; b++) {
            ST8(obj + BTN_CUR + b, out[b]);
            if (out[b]) { mask |= BTN_BIT[b]; in_presses++; }
        }
        if (want_mask)   ST8(mask_buf + i, mask);
        if (want_mirror) ST8(MASK_MIRROR + i, mask);
    }
}
