/* The game's own model of its world: where the players, the objects and the object data
 * live in guest memory, and what this port may do to them.
 *
 * Every address and offset here was LOCATED -- against the game's own code or its own data
 * files -- rather than found by trying numbers until a fighter appeared. Each carries the
 * evidence with it, because an offset with no provenance is indistinguishable from an
 * offset that happened to work on one run. Issues #15 and #16 are the long form.
 */
#ifndef LF2_WORLD_H
#define LF2_WORLD_H

#include <stdint.h>

enum { DEVSEL = 0x00450b4c, DEVSEL_END = 0x00450b6c };

/* The game's player count, and it is the table's own: DEVSEL_END - DEVSEL is 32 bytes, one
 * dword of control-config index per slot. Those slots are table indices 0..7, which is why
 * a drop-in join has to take a free one of the FIRST EIGHT object entries rather than any
 * free index -- the high indices the game gives its computer opponents have no selector. */
enum { PLAYER_SLOTS = (DEVSEL_END - DEVSEL) / 4 };

/* Which players have joined, as a bitmask -- bit i is player i. Found by diffing .data
 * across a character-select join (0 -> 1) and again across a second join (1 -> 3), which is
 * what tells a mask from a count. */
enum { JOINED_MASK = 0x00451288 };

/* this+404: FOUR HUNDRED object pointers, not eight. The eight player slots are its first
 * eight entries; the fighter the game gives a computer opponent lands further up the same
 * array on the same 0x420 stride. Every entry is a live pointer to a pre-allocated record
 * from the moment the data loads, so an object's existence is not its pointer being there.
 *
 * What decides existence is EXISTS: a byte per index at this+4, and `fn_004064d0` walks the
 * table as
 *
 *     ESI = this + 404
 *     EAX = LD32(ESI)                  // obj = table[k]
 *     if (obj->0x338 > 0) obj->0x338--;   // a countdown, run for every entry
 *     if (LD8(this + 4 + k) != 1) goto next
 *
 * -- which is why an idle entry is read exactly once a frame (the countdown) and nothing
 * more, and why filling in a record and setting the joined-players mask was never going to
 * be enough on its own. */
enum { PLAYER_PTRS = 404, TABLE_N = 400, OBJ_STRIDE = 0x420 };
enum { EXISTS = 0x00458b04 };          /* this+4: one byte per object index, 1 = exists */

/* this+2004: a pointer to the object-data registry -- an array of pointers to per-object
 * data blocks, with its entry count at a fixed (large) offset from the base. Field 1780 of
 * a block is the object id from data.txt. Both offsets are the game's own, read off the
 * spawn inlined in fn_0041bc90. */
enum { REG_PTR = 2004, REG_COUNT_OFF = 81273728 };

/* A data block's id and its TYPE, adjacent. Both are located against the game's own
 * data.txt, whose <object> section declares an id and a type for each entry -- and the
 * registry is that list, in file order, all 65 of them. The type offset was required to
 * match the declared type on EVERY entry, not a sample: +1784 is the only offset in the
 * first 2048 bytes that does, at byte, word and dword width alike.
 *
 * type 0 is a character. The rest are weapons, throwables, effects and the criminal. */
enum { DATA_ID = 1780, DATA_TYPE = 1784, DATA_TYPE_CHARACTER = 0 };

/* The template object, data\\template.dat. It is type 0 and so counts as a character, but
 * it is the template rather than a playable fighter, and the character-select screen does
 * not offer it. Named by id because that is what it is -- not an offset that happened to
 * work. */
enum { DATA_ID_TEMPLATE = 0 };
enum { BTN_CUR = 205 };                /* obj+205..211: this frame's seven buttons */

/* Where a fighter stands: the three ints and the three doubles the game's own spawn site
 * copies from another object. Carried as a value so a fighter can be REBUILT in the place
 * it already occupied, which is what cycling a character during selection needs -- taking
 * the position from a live fighter again each time would walk the preview across the stage
 * one press at a time. */
struct coop_pos { uint32_t i16, i20, i24; double d88, d96, d104; };

/* ---- coop.c: the mechanism ---- */

/* The registry entry whose data block carries object id `id`, or 0 having said why. */
uint32_t coop_data_for_id(uint32_t self, int id);

/* The game's roster of playable characters, in registry order. Returns how many, or -1. */
int  coop_roster(uint32_t self, int ids[], int max);
int  coop_random_character(uint32_t self, long seed);

void coop_pos_from(uint32_t ref, double dx, struct coop_pos *p);

/* Build object `id` into table entry `dst`, by the game's own sequence. Clears the gate
 * first, so it is equally a REBUILD; `watch` latches the follow-up watch onto it. */
void coop_build(uint32_t self, int dst, int id, const struct coop_pos *p, int sel, int watch);

/* The LF2_COOP_SPAWN probe's front door: coop_build at a position taken from a live
 * fighter, refusing loudly for any entry where a new fighter would prove nothing. */
void coop_spawn(uint32_t self, int dst, int id, int posref, int sel);

/* Is a MATCH on screen -- as opposed to character selection, which shares its mode? */
int  coop_match_running(uint32_t self);

/* Join `slot` as a player: a fighter, its device selector and its bit in the joined mask.
 * `pos_out` receives where it was built, for a caller that will rebuild it in place. */
int  coop_join(uint32_t self, int slot, int id, struct coop_pos *pos_out, int watch);

/* The inverse, field for field -- and refused for a slot this port did not fill. */
void coop_leave(uint32_t self, int slot, const char *why);

/* A late joiner's character selection: a flashing fighter, left/right to cycle, attack to
 * lock in. `tick` is given the device's buttons this frame and last, so every press in it
 * is an edge. */
int  coop_select_begin(uint32_t self, int slot, int dev);
void coop_select_tick(uint32_t self, int slot, const unsigned char btn[7],
                      const unsigned char prev[7]);

int  coop_selecting(int slot);         /* withhold this slot's buttons from its fighter */
int  coop_owns(int slot);              /* this port put the fighter there, so it may remove it */
void coop_reset(void);                 /* the game proper was left */

/* ---- coop_debug.c: the instruments ---- */

int  coop_entry_live(uint32_t p);      /* is this record something other than the idle default */
void coop_watch_latch(int dst, uint32_t obj);
void coop_spawn_watch(uint32_t self);
void coop_refs_scan(uint32_t self);
void coop_pair_diff(uint32_t self, int i, int j);
void coop_table_dump(uint32_t self);
void coop_registry_dump(uint32_t self);
void coop_debug_tick(uint32_t self);   /* every LF2_COOP_* probe, once per gather */

#endif
