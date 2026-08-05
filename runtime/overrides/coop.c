/* Drop-in coop: putting a fighter into a running match, and choosing which one.
 *
 * One of the hand-written native replacements for recompiled functions; see
 * runtime/overrides/overrides.h for how the set is divided and why.
 */

#include "overrides.h"
#include "world.h"

#include "../guest_ops.h"
#include "../guest_map.h"
#include "../hostwin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- LF2_COOP_SPAWN=<dst>[,<src>] -- put a fighter in the world by imitating the game ----
 *
 * It builds the fighter the way the GAME does, not by copying a neighbour. A clone was the
 * right probe for "is the gate the gate" -- it answered that -- but it can only ever
 * duplicate a fighter already on the stage, because it copies the source's character, HP
 * and everything else. The real sequence is inlined in fn_0041bc90 around 0x004211db:
 *
 *     reg   = LD32(this + 2004)                   // the object-data registry
 *     count = LD32(reg + 81273728)
 *     find i with LD32(LD32(reg + 4i) + 1780) == <the object id wanted>
 *     obj = LD32(this + 404 + 4k)
 *     ECX = obj; fn_004061d0()                    // __thiscall reset of the record
 *     obj->872 = LD32(reg + 4i)                   // the object-data pointer
 *     obj->796 = data->144
 *     this[4 + k] = 1                             // the gate
 *     obj->16 / +20 / +24 and the doubles at +88 / +96 / +104 <- position
 *
 * Field 1780 of a data block IS the object id from data.txt, and that is checked rather
 * than assumed: all 65 registry entries carry an id that appears in the game's own
 * data.txt, with no exceptions, and the only two data.txt ids NOT in the registry are 3 and
 * 12, which are backgrounds rather than objects. It also settles fn_004064d0's comparison
 * of the same field against 7 and 8 -- those are Firen and Freeze, not a type code.
 *
 * Position is copied from a live fighter and offset. That is not a leftover of the clone:
 * it is what the game's own spawn site does, copying +16/+20/+24 and the doubles from
 * another object.
 *
 * NOT ESTABLISHED, and written here rather than hidden: what +0x354 means. fn_004061d0
 * resets it to 99, another spawn site copies it from the spawning object, and the computer
 * opponent the game itself put at entry 11 holds 11 -- its own index. Setting it to the
 * destination index is an imitation of the one in-match registration that can be observed,
 * not a rule derived from the code, and it is what gives the new fighter its HUD bar.
 *
 * It then WATCHES the record for the following frames rather than declaring victory on the
 * write. The interesting failure is not "nothing appeared on screen" -- it is the record
 * being reset by the game's own sweep a frame later, which looks identical from outside and
 * means something quite different. */
void fn_004061d0(void);                  /* __thiscall: reset the object record in ECX */

/* The registry entry whose data block carries object id `id`, or 0. Refuses out loud
 * rather than returning 0 for two different reasons. */
uint32_t coop_data_for_id(uint32_t self, int id)
{
    const uint32_t reg = LD32(self + REG_PTR);
    if (!reg || reg < GUEST_HEAP_BASE || reg >= GUEST_HEAP_END) {
        fprintf(stderr, "coop spawn: REFUSED -- this+%d = %08x is not a heap pointer, so the "
                        "registry was never read\n", REG_PTR, reg);
        return 0;
    }
    const int32_t count = (int32_t)LD32(reg + (uint32_t)REG_COUNT_OFF);
    if (count <= 0 || count > 512) {
        fprintf(stderr, "coop spawn: REFUSED -- registry count reads %d, which is not "
                        "plausible; nothing was searched\n", count);
        return 0;
    }
    for (int i = 0; i < count; i++) {
        const uint32_t d = LD32(reg + 4u * (uint32_t)i);
        if (d >= GUEST_HEAP_BASE && d < GUEST_HEAP_END && (int32_t)LD32(d + DATA_ID) == id)
            return d;
    }
    fprintf(stderr, "coop spawn: REFUSED -- no data block with object id %d among the %d "
                    "registry entries (they were ALL examined)\n", id, count);
    return 0;
}

/* The game's own roster of playable characters: the registry entries whose type field says
 * character, less the template. Fills `ids` in registry order and returns how many, or -1
 * having said out loud which step failed.
 *
 * It never returns an empty roster quietly. "This game has no playable characters" and "the
 * registry was never read" are different statements, and a caller handed 0 for both would
 * report a broken read as a game with nobody in it. */
int coop_roster(uint32_t self, int ids[], int max)
{
    const uint32_t reg = LD32(self + REG_PTR);
    if (!reg || reg < GUEST_HEAP_BASE || reg >= GUEST_HEAP_END) {
        fprintf(stderr, "coop roster: REFUSED -- this+%d = %08x is not a heap pointer, so "
                        "the registry was never read and NO entry was examined\n",
                REG_PTR, reg);
        return -1;
    }
    const int32_t count = (int32_t)LD32(reg + (uint32_t)REG_COUNT_OFF);
    if (count <= 0 || count > 512) {
        fprintf(stderr, "coop roster: REFUSED -- registry count reads %d, which is not "
                        "plausible; NO entry was examined\n", count);
        return -1;
    }
    int n = 0, skipped = 0;
    for (int i = 0; i < count; i++) {
        const uint32_t d = LD32(reg + 4u * (uint32_t)i);
        if (d < GUEST_HEAP_BASE || d >= GUEST_HEAP_END) continue;
        if ((int32_t)LD32(d + DATA_TYPE) != DATA_TYPE_CHARACTER) continue;
        const int id = (int32_t)LD32(d + DATA_ID);
        if (id == DATA_ID_TEMPLATE) continue;
        if (n < max) ids[n++] = id; else skipped++;
    }
    if (n == 0)
        fprintf(stderr, "coop roster: all %d registry entries were examined and NONE is a "
                        "playable character (type %d, id other than %d)\n",
                count, DATA_TYPE_CHARACTER, DATA_ID_TEMPLATE);
    if (skipped)
        fprintf(stderr, "coop roster: %d characters past the %d this build can hold were "
                        "DROPPED -- the cycle does not reach them\n", skipped, max);
    return n;
}

/* A playable character's object id from that roster. Returns 0 and says why if the roster
 * cannot be read, rather than falling back to a hard-coded id -- a silent fallback would
 * make a broken registry read look like a working feature.
 *
 * The pick is DETERMINISTIC, from the frame the join happens on. A late joiner wants a
 * varied character, not an unpredictable one, and a run that cannot be reproduced is worse
 * to debug than a run that always picks the same fighter. */
int coop_random_character(uint32_t self, long seed)
{
    int ids[256];
    const int n = coop_roster(self, ids, (int)(sizeof ids / sizeof ids[0]));
    if (n <= 0) return 0;                 /* coop_roster has already said why */
    const int pick = ids[(int)(((unsigned long)seed) % (unsigned long)n)];
    fprintf(stderr, "coop: %d playable characters on the game's roster, picked id %d\n",
            n, pick);
    return pick;
}

void coop_pos_from(uint32_t ref, double dx, struct coop_pos *p)
{
    p->i16  = (uint32_t)((int32_t)LD32(ref + 16) + (int32_t)dx);
    p->i20  = LD32(ref + 20);
    p->i24  = LD32(ref + 24);
    p->d88  = LDF64(ref + 88) + dx;
    p->d96  = LDF64(ref + 96);
    p->d104 = LDF64(ref + 104);
}

/* Build object `id` into table entry `dst` at `p`, by the game's own sequence. The gate is
 * CLEARED first, so this is equally a rebuild of an entry that is already in the world --
 * the caller decides whether overwriting one is legitimate.
 *
 * `watch` latches the follow-up watch onto this entry. A preview rebuild passes 0: the
 * watch reports ages since a spawn, and restarting it every time a joiner presses left
 * would make its samples measure the last keypress rather than the fighter's life. */
void coop_build(uint32_t self, int dst, int id, const struct coop_pos *p, int sel,
                       int watch)
{
    const uint32_t d = LD32(self + PLAYER_PTRS + 4u * (uint32_t)dst);
    if (!d) {
        fprintf(stderr, "coop build: REFUSED -- table entry %d is null, nothing written\n", dst);
        return;
    }
    const uint32_t data = coop_data_for_id(self, id);
    if (!data) return;                    /* coop_data_for_id has already said why */

    ST8(EXISTS + (uint32_t)dst, 0);       /* out of the world while the record is rewritten */

    /* The game's own reset, called in its own ABI: the lifted body pops the return address
     * itself, so the caller pushes one. It preserves EBX/ESI/EBP, which is what the
     * recompiled caller of this gather expects. */
    PUSH32(0x00421243u);
    R(ECX) = d;
    fn_004061d0();

    ST32(d + 872, data);                              /* the object-data pointer */
    ST32(d + 796, LD32(data + 144));
    ST32(d + 16, p->i16);
    ST32(d + 20, p->i20);
    ST32(d + 24, p->i24);
    STF64(d + 88,  p->d88);
    STF64(d + 96,  p->d96);
    STF64(d + 104, p->d104);
    ST32(d + 852, (uint32_t)dst);                     /* see the note above: imitation */
    /* The third field of LF2_COOP_SPAWN, for the portrait A/B. fn_004061d0 zeroes +0x364,
     * and a spawned fighter's HUD portrait is wrong, so this exists to test whether that
     * field is what the HUD reads -- two otherwise identical spawns with different values
     * either draw different portraits or they do not, and one run of each settles it. */
    if (sel >= 0) ST32(d + 0x364, (uint32_t)sel);
    ST8(EXISTS + (uint32_t)dst, 1);                   /* the gate fn_004064d0 tests */

    if (watch) {
        coop_watch_latch(dst, d);
        fprintf(stderr, "coop spawn: built object id %d at table entry %d (%08x) from "
                        "registry data %08x at frame %ld; gate byte %08x set\n",
                id, dst, d, data, hostwin_frames(), EXISTS + (uint32_t)dst);
        if (sel >= 0)
            fprintf(stderr, "coop spawn: +0x364 forced to %d\n", sel);
    }
}

void coop_spawn(uint32_t self, int dst, int id, int posref, int sel)
{
    const uint32_t d = LD32(self + PLAYER_PTRS + 4u * (uint32_t)dst);
    const uint32_t ref = posref >= 0 ? LD32(self + PLAYER_PTRS + 4u * (uint32_t)posref) : 0;
    if (!d) {
        fprintf(stderr, "coop spawn: REFUSED -- table entry %d is null, nothing written\n", dst);
        return;
    }
    if (LD8(EXISTS + (uint32_t)dst)) {
        fprintf(stderr, "coop spawn: REFUSED -- entry %d already has its gate byte set, so a "
                        "fighter appearing there would prove nothing\n", dst);
        return;
    }
    if (!ref) {
        fprintf(stderr, "coop spawn: REFUSED -- no live fighter to take a spawn position "
                        "from, so this frame is not a match\n");
        return;
    }
    /* Each spawn is pushed further along x than the last, so two of them in one run do not
     * land on top of each other and read as one fighter. */
    static int spawn_n;
    struct coop_pos p;
    coop_pos_from(ref, 120.0 * (double)(++spawn_n), &p);
    coop_build(self, dst, id, &p, sel, 1);
    if (LD8(EXISTS + (uint32_t)dst))
        fprintf(stderr, "coop spawn: position taken from entry %d\n", posref);
}

/* ---- LF2_COOP_JOIN=<slot>[,<object id>] -- spawn into a PLAYER slot ----
 *
 * The spawn above can put a fighter at any of the 400 table indices. A joining player takes
 * one of the FIRST EIGHT, because the gather -- the game's and this port's alike -- reaches
 * a player's fighter as `this+404+4i` for the eight entries of the device-selector table,
 * so a fighter anywhere else has nothing writing buttons into it.
 *
 * On top of the spawn this sets the device-selector entry (the control-config index) and
 * the bit in the joined-players mask at 0x00451288.
 *
 * PLAYER SLOT i IS OBJECT INDEX i, and that is the game's own code rather than this port's
 * assumption. fn_00419a60__orig walks the two arrays in lockstep:
 *
 *     EBP = 0x450b4c            // &devsel[0]
 *     EAX = this + 404          // &table[0]
 *   loop:
 *     if ((int32_t)LD32(EBP) <= 0) goto next
 *     ESI = LD32(EAX)           // the object whose buttons this slot's config drives
 *     ...
 *   next:
 *     EBP += 4; EAX += 4; ECX += 1
 *     if (EBP < 0x450b6c) goto loop
 *
 * So a HUMAN player's fighter has to be at its own index -- there is no other route by
 * which the game could deliver its buttons -- and eight is the game's bound, not a guess.
 *
 * A COMPUTER's fighter is not bound by that, because its AI writes buttons straight into
 * whatever object it drives and never goes through the gather. That is what reconciles the
 * odd reading in a normal match: the joined mask says two players while object index 1 is
 * empty and the computer opponent is at index 11. The mask tracks the character-select
 * roster, in which a slot marked "Computer" counts as taken; it does not say what occupies
 * the player object slots.
 *
 * A consequence worth knowing before it surprises someone: joining into a slot the game
 * filled with a computer does NOT replace that computer. Its fighter is at its own high
 * index and stays on the stage, so the match gains a fighter rather than swapping one. */
/* Is a MATCH on screen? Character selection and the fight share a top-level mode, so this
 * needs a real signal, and two home-made ones were wrong before the right one was used.
 *
 *   1. "something has its gate byte set". FALSE: tracking entry 1 through character
 *      selection shows its gate byte going up and down there with the object still at the
 *      origin. It merely happened to be clear at the instant a joining pad was seen, so the
 *      drop-in never misfired -- luck, not a guarantee.
 *   2. "and the character-select panel is not up". Still wrong: there is a window before
 *      that panel is first drawn where a gate byte is already set, and a position sampled
 *      in it enters a movement measurement as a jump from x=0.
 *
 * The port already HAD the answer. panel_hud_up() is the in-match HUD strip, drawn only
 * while the world view is up -- the widescreen code depends on it for exactly this
 * distinction. Using it is not a third heuristic; it is the established one. */
int coop_match_running(uint32_t self)
{
    if (!panel_hud_up() || panel_charselect_up() || panel_overlay_up()) return 0;
    for (int k = 0; k < TABLE_N; k++)
        if (LD8(EXISTS + (uint32_t)k) && LD32(self + PLAYER_PTRS + 4u * (uint32_t)k))
            return 1;
    return 0;
}

/* The state of a late joiner's character selection, and of the slots this port filled.
 * Declared here rather than beside coop_select_begin because coop_leave, further up, has
 * to abandon a choice in progress -- a slot cannot leave and still be choosing. */
static int  sel_active[PLAYER_SLOTS];
static int  sel_pick[PLAYER_SLOTS];       /* index into the roster below */
static int  sel_shown[PLAYER_SLOTS];      /* whether the HUD panel shows the candidate now */
static long sel_flash0[PLAYER_SLOTS];     /* frame the current flash cycle started */
static long sel_since[PLAYER_SLOTS];      /* frame selection began */
static long sel_flashes[PLAYER_SLOTS];    /* times the panel went from shown to hidden */
static struct coop_pos sel_pos[PLAYER_SLOTS];

/* Slots THIS PORT put a fighter into. Drop-out only ever undoes a join this code made: a
 * slot the game's own character selection filled is not ours to empty, and a pad being
 * unplugged is not a reason to delete a fighter somebody else's device is driving. */
static int coop_owned[PLAYER_SLOTS];

/* Returns 1 if the slot ended up in the world. `pos_out`, when given, receives the position
 * the fighter was built at, so a caller that will rebuild it in place has it. */
int coop_join(uint32_t self, int slot, int id, struct coop_pos *pos_out, int watch)
{
    if (slot < 0 || slot >= PLAYER_SLOTS) {
        fprintf(stderr, "coop join: REFUSED -- slot %d is outside the eight player slots "
                        "(the device selector table has exactly eight entries), so it could "
                        "never be a player. Nothing was written.\n", slot);
        return 0;
    }
    if (LD8(EXISTS + (uint32_t)slot)) {
        fprintf(stderr, "coop join: REFUSED -- player slot %d is already in the world\n", slot);
        return 0;
    }
    int posref = -1;
    for (int k = 0; k < TABLE_N; k++)
        if (LD8(EXISTS + (uint32_t)k)) { posref = k; break; }
    const uint32_t ref = posref >= 0 ? LD32(self + PLAYER_PTRS + 4u * (uint32_t)posref) : 0;
    if (!ref) {
        fprintf(stderr, "coop join: REFUSED -- no live fighter to take a spawn position "
                        "from, so this frame is not a match\n");
        return 0;
    }

    const uint32_t sel_before = LD32(DEVSEL + 4u * (uint32_t)slot);
    const uint32_t mask_before = LD32(JOINED_MASK);

    /* Each join is pushed further along x than the last, so two of them in one run do not
     * land on top of each other and read as one fighter. */
    static int join_n;
    struct coop_pos p;
    coop_pos_from(ref, 120.0 * (double)(++join_n), &p);
    coop_build(self, slot, id, &p, -1, watch);
    if (!LD8(EXISTS + (uint32_t)slot)) {
        fprintf(stderr, "coop join: the spawn refused, so nothing else was set\n");
        return 0;
    }
    if (pos_out) *pos_out = p;
    coop_owned[slot] = 1;

    ST32(DEVSEL + 4u * (uint32_t)slot, (uint32_t)(slot + 1));   /* control config, 1-based */
    ST32(JOINED_MASK, mask_before | (1u << slot));
    fprintf(stderr, "coop join: slot %d -- position from entry %d, devsel %d -> %d, joined "
                    "mask %08x -> %08x\n",
            slot, posref, (int32_t)sel_before, slot + 1, mask_before, LD32(JOINED_MASK));
    return 1;
}

/* Drop-out: take back everything coop_join set. The inverse of the join, field for field,
 * so a slot that leaves is indistinguishable from one that never joined -- and it is
 * REFUSED for a slot this port did not fill, which is the case that would otherwise delete
 * a fighter belonging to the game's own roster. */
void coop_leave(uint32_t self, int slot, const char *why)
{
    (void)self;
    if (slot < 0 || slot >= PLAYER_SLOTS) return;
    if (!coop_owned[slot]) {
        fprintf(stderr, "coop leave: REFUSED -- slot %d was not filled by this port (%s), so "
                        "it is not this code's to empty; nothing was written\n", slot, why);
        return;
    }
    const uint32_t mask_before = LD32(JOINED_MASK);
    sel_active[slot] = 0;                             /* any half-made choice goes with it */
    ST8(EXISTS + (uint32_t)slot, 0);                  /* out of the world */
    ST32(DEVSEL + 4u * (uint32_t)slot, 0);
    ST32(JOINED_MASK, mask_before & ~(1u << slot));
    coop_owned[slot] = 0;
    fprintf(stderr, "coop leave: slot %d dropped out (%s) -- gate cleared, devsel -> 0, "
                    "joined mask %08x -> %08x\n", slot, why, mask_before, LD32(JOINED_MASK));
}

/* ---- a late joiner's character selection ----
 *
 * A player who joins after the match started missed the character-select screen, so the
 * choice is made IN THAT PLAYER'S HUD PANEL: the candidate's portrait and bars appear in
 * the empty box along the top of the screen and flash there, while its device cycles
 * left/right through the game's roster and confirms with attack (A on a pad). THE FIGHTER
 * IS NOT ON THE STAGE until the lock-in. It cannot walk, be hit, or be seen; the match
 * carries on around a player who is still deciding.
 *
 * It was built the other way first -- the fighter standing in the match, flashing, while
 * its player chose -- and that is issue #19: a blinking body in the middle of a fight.
 *
 * HOW ONE SLOT CAN HAVE A PANEL AND NO FIGHTER, because the first attempt at this failed
 * on exactly that and the reason is worth keeping. Both the HUD and the stage read the SAME
 * per-slot byte, `this+4+i` -- the port's EXISTS:
 *
 *     fn_0041ae60  (the HUD strip)   for i in 0..7: if (this[4+i]) draw the panel from
 *                                    this+404+4i; else if (this[0xe+i]) draw a computer's
 *                                    from this+404+4(i+10); else leave the box empty
 *     fn_0041a5a0  (the stage)       collect every k in 0..399 with this[4+k], depth-sort
 *                                    them, draw each
 *     fn_004064d0  (the world step)  the same byte again
 *
 * so simply not building the fighter leaves no panel to choose in, and building it puts a
 * body on the stage. The two passes have to disagree, and the only place they can is
 * BETWEEN them: fn_0041ae60 is overridden in hud.c to raise the byte for a slot that is
 * still choosing, call the game's own panel drawing, and put it back down. The stage pass
 * and the world step run outside that window and never see the slot, so the joiner is a
 * HUD entry and nothing else -- and the panel that appears is the game's own, drawn by the
 * game's own code from the game's own record, not a picture this port painted.
 *
 * WHAT THE FLASH IS, stated plainly because it is not what "fade in/out" would mean on a
 * modern renderer: the panel is drawn on some frames and skipped on others. It is NOT an
 * alpha fade. The port's blit path is a colour-keyed copy of 8-bit paletted sprites --
 * there is no blend anywhere in it, and nothing in the game's own data carries an alpha
 * channel -- so a true fade would mean inventing per-object blending in the porting layer
 * rather than using the game's.
 *
 * Cycling REBUILDS the record: gate off, the game's own reset, the new character's data
 * block, the same position back. That is the spawn sequence, not an edit -- a character's
 * animation frame numbers do not carry over to another character, so keeping the record and
 * only swapping its data pointer would leave the new fighter holding a frame that belongs
 * to the old one. The rebuild is also where the panel's portrait, name and bars come from:
 * they are read off that record, so the panel follows the cycle without this port knowing
 * anything about how a portrait is drawn.
 *
 * The device's buttons are withheld from the record as well, so that nothing is left in it
 * when the fighter does arrive -- left and right are choosing a character, and the record
 * must not be holding a direction on the frame it enters the world. */
enum { SELECT_FLASH = 8 };                /* frames the panel shows, then the same hidden */


static int roster_ids[128];
static int roster_n;                      /* 0 = not read yet, <0 = the read refused */

static int coop_roster_ready(uint32_t self)
{
    if (roster_n == 0)
        roster_n = coop_roster(self, roster_ids, (int)(sizeof roster_ids / sizeof roster_ids[0]));
    return roster_n > 0;
}

/* Begin selection for `slot`, driven by device `dev`. Returns 1 if a fighter is on the
 * stage to choose with. */
int coop_select_begin(uint32_t self, int slot, int dev)
{
    if (slot < 0 || slot >= PLAYER_SLOTS) return 0;
    if (!coop_roster_ready(self)) {
        fprintf(stderr, "coop select: the game's roster could not be read (see above), so "
                        "device %d has nothing to choose between and joins nothing\n", dev);
        return 0;
    }

    /* Where the cycle STARTS. LF2_COOP_CHAR pins it, and says so when it names a character
     * the roster does not have rather than silently starting somewhere else. */
    int start = (int)(((unsigned long)(hostwin_frames() + dev)) % (unsigned long)roster_n);
    const char *ch = getenv("LF2_COOP_CHAR");
    if (ch && atoi(ch) > 0) {
        const int want = atoi(ch);
        int at = -1;
        for (int i = 0; i < roster_n; i++) if (roster_ids[i] == want) at = i;
        if (at >= 0) start = at;
        else fprintf(stderr, "coop select: LF2_COOP_CHAR=%d is not among the %d characters "
                             "on the game's roster, so the cycle starts at id %d instead\n",
                     want, roster_n, roster_ids[start]);
    }

    if (!coop_join(self, slot, roster_ids[start], &sel_pos[slot], 0)) return 0;

    /* And straight back OUT of the world. coop_join leaves the gate up because that is what
     * joining means; a player who is still choosing has not joined the fight yet, and the
     * panel it chooses in is raised for the HUD pass alone (hud.c). Everything else the join
     * set -- the record, the device selector, the bit in the joined mask -- stays, because
     * the slot IS taken; it just has nobody standing in it. */
    ST8(EXISTS + (uint32_t)slot, 0);

    sel_active[slot] = 1;
    sel_pick[slot]   = start;
    sel_shown[slot]  = 1;
    sel_flash0[slot] = hostwin_frames();
    sel_since[slot]  = hostwin_frames();
    sel_flashes[slot] = 0;
    fprintf(stderr, "coop select: slot %d is choosing -- %d characters on the roster, "
                    "starting at id %d; left/right cycles, attack locks in\n",
            slot, roster_n, roster_ids[start]);
    fprintf(stderr, "coop select: slot %d is OUT of the world while it chooses -- its panel "
                    "is raised for the HUD pass alone (gate byte %08x = %d outside the HUD "
                    "pass)\n",
            slot, EXISTS + (uint32_t)slot, LD8(EXISTS + (uint32_t)slot));
    return 1;
}

/* One frame of a slot's selection: the flash, the cycling, and the lock-in. `prev` is the
 * device's buttons LAST frame, so every press here is an edge -- a held direction must not
 * run through the roster at thirty characters a second. */
void coop_select_tick(uint32_t self, int slot, const unsigned char btn[7],
                             const unsigned char prev[7])
{
    if (!sel_active[slot]) return;
    const long f = hostwin_frames();
    /* The press that CLAIMED the slot is still down on the frame selection began; treating
     * it as a lock-in would end the choice before the player saw it start. */
    if (f == sel_since[slot]) return;

    /* The record can be gone from under us: the game resets a table entry on its own when a
     * round ends. Selection ends with it rather than rebuilding a fighter into a match that
     * is no longer running. */
    if (!coop_match_running(self)) {
        fprintf(stderr, "coop select: slot %d was still choosing when the match stopped "
                        "running, so the selection is abandoned at id %d\n",
                slot, roster_ids[sel_pick[slot]]);
        sel_active[slot] = 0;
        return;
    }

    const int left  = btn[2] && !prev[2];
    const int right = btn[3] && !prev[3];
    const int lock  = btn[4] && !prev[4];

    if (lock) {
        sel_active[slot] = 0;
        /* THE ONE PLACE THE FIGHTER ENTERS THE WORLD. coop_build leaves the gate up, and
         * with the selection over nothing puts it back down, so from this frame the record
         * is on the stage, stepped by the world and driven by its pad. Watched from HERE,
         * because the fighter's life as a player starts at the lock-in, not at the first
         * preview. */
        coop_build(self, slot, roster_ids[sel_pick[slot]], &sel_pos[slot], -1, 1);
        fprintf(stderr, "coop select: slot %d LOCKED IN character id %d after %ld frames "
                        "of choosing, having flashed %ld times; it enters the world NOW "
                        "(gate byte %08x = %d)\n",
                slot, roster_ids[sel_pick[slot]], f - sel_since[slot], sel_flashes[slot],
                EXISTS + (uint32_t)slot, LD8(EXISTS + (uint32_t)slot));
        return;
    }

    if (left || right) {
        const int before = roster_ids[sel_pick[slot]];
        sel_pick[slot] = (sel_pick[slot] + (right ? 1 : roster_n - 1)) % roster_n;
        /* The new character appears at once and the flash restarts from shown, so a press
         * always produces a visible answer instead of landing in a dark half. */
        sel_flash0[slot] = f;
        sel_shown[slot]  = 1;
        coop_build(self, slot, roster_ids[sel_pick[slot]], &sel_pos[slot], -1, 0);
        ST8(EXISTS + (uint32_t)slot, 0);      /* rebuilt, still not in the fight */
        fprintf(stderr, "coop select: slot %d cycled %s, id %d -> %d (%d of %d)\n",
                slot, right ? "right" : "left", before, roster_ids[sel_pick[slot]],
                sel_pick[slot] + 1, roster_n);
        return;
    }

    const int show = (((f - sel_flash0[slot]) / SELECT_FLASH) & 1) == 0;
    if (show == sel_shown[slot]) return;
    sel_shown[slot] = show;
    if (!show) sel_flashes[slot]++;

    /* The flash is the one part of this that is a SCHEDULE rather than a press, so it is
     * the part that can silently not happen -- and a panel that simply sits there looks,
     * from outside, exactly like one that is flashing too fast to see. The first few
     * transitions are printed with the frame they happened on, so the period is readable,
     * and the total is reported at lock-in. Capped at four rather than gated off, because a
     * player may spend a minute choosing and a line every eight frames would be a wall.
     *
     * The gate byte is printed with them, and it must read 0 in BOTH halves: it is raised
     * only for the duration of the HUD pass, which is not now. A 1 here would mean the
     * joiner is standing on the stage, which is the whole of issue #19. */
    if (sel_flashes[slot] <= 4)
        fprintf(stderr, "coop select: slot %d frame %ld -- panel %s (gate byte %08x = %d "
                        "outside the HUD pass)\n",
                slot, f, show ? "shown" : "hidden", EXISTS + (uint32_t)slot,
                LD8(EXISTS + (uint32_t)slot));
}

/* For hud.c: should this slot's panel be drawn this frame from a record that is not in the
 * world? True only while it is choosing and only in the lit half of the flash. */
int coop_hud_preview(int slot)
{
    return slot >= 0 && slot < PLAYER_SLOTS && sel_active[slot] && sel_shown[slot];
}

/* What the input gather needs to know about a slot without reaching into the state above.
 * `coop_selecting` is why a device's buttons are withheld from its fighter; `coop_owns` is
 * why a disconnected pad may take that fighter with it. */
int coop_selecting(int slot)
{
    return slot >= 0 && slot < PLAYER_SLOTS && sel_active[slot];
}

int coop_owns(int slot)
{
    return slot >= 0 && slot < PLAYER_SLOTS && coop_owned[slot];
}

/* Leaving the game proper ends the match that every joined slot and every half-made choice
 * belonged to. The ROSTER is deliberately kept: it is the data file's, not the match's, and
 * re-reading it per match would repeat a scan whose answer cannot have changed. */
void coop_reset(void)
{
    for (int i = 0; i < PLAYER_SLOTS; i++) { sel_active[i] = 0; coop_owned[i] = 0; }
}
