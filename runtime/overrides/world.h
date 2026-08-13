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

#include "geom.h"

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
int  coop_hud_preview(int slot);       /* hud.c: raise this slot for the HUD pass alone */
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


/* ---- the stage's background layers ----
 *
 * A background is a list of LAYERS, each a bitmap with a SPAN and an optional LOOP, and the
 * two are different fields with different jobs. Both were read straight out of fn_0041a250,
 * which is the whole layer draw and is short enough to read end to end:
 *
 *     span   = bg.dat's `width:`   BG_LAYER_SPAN   -- how far the layer scrolls, and how far
 *                                                    its content reaches
 *     loop   = bg.dat's `loop:`    BG_LAYER_LOOP   -- the horizontal repeat step, 0 = none
 *
 * The draw is, in the game's own terms:
 *
 *     off = -(camera * (span - 794)) / (stage_width - 794)      // the parallax
 *     if (loop)  for (x = layer_x; x < span; x += loop)  draw(off + x)
 *     else       draw(off + layer_x)
 *
 * and the 794 is the game's screen width, appearing ONLY in that parallax -- not, as an
 * earlier note in this file had it, as a loop bound. What the formula buys is the property
 * the whole design rests on: a layer's span is chosen so that the layer covers the screen at
 * EVERY camera position, exactly, with no margin. At camera 0 the layer's left edge is at
 * screen 0; at the camera's maximum its right edge is at screen 794. Brokeback Clif's cliffs
 * span 1379 over a 1500-wide stage, and 1379 - 794 == 585 == the offset measured at maximum
 * camera, to the pixel.
 *
 * That is why widescreen leaves black beside a sky (#23) and why tiling it is not the answer:
 * a non-looping layer has EXACTLY 794 pixels' worth of picture at any camera and no more. A
 * LOOPING layer is different -- it declares its own repeat, so carrying it past the game's
 * 794 is the game's layout continued rather than invented.
 *
 * The table is heap-resident, so there is no address to hardcode -- and an earlier pass
 * confirmed there is no pointer to it in .data or anywhere in the heap. It is reached the
 * way the game reaches it:
 *
 *     registry = LD32(BG_REGISTRY)                  // the world object's registry pointer
 *     bg       = LD32(BG_INDEX)                     // which background is loaded
 *     field[i] = LD32(registry + (bg*BG_STRIDE_DW + i)*4 + <field constant>)
 *
 * Each background record is 612 dwords (2448 bytes) and each layer field a 30-entry array.
 * The two per-BACKGROUND fields below are addressed the same way but with no layer index.
 */
enum { BG_REGISTRY = 0x00458b00 + 2004,
       BG_INDEX = 0x0044d024,
       BG_RANDOM = 0x0044d028 }; /* pre-fight "Random Background" flag; fn_00429730
                                  * replaces BG_INDEX on confirm only while this is 1 */

/* THE TWO WORDS THE GAME ENTERS ITSELF THROUGH (issue #22).
 *
 * GAME_TOP_MODE is the FIRST DWORD OF THE WORLD OBJECT -- the same object coop.c calls
 * `this`. fn_004246b0 is called with ECX = 0x00458b00 and switches on [ECX]: 0 is the
 * launcher, 1 loads, 2 is the game proper. Exhaustively over the lifted binary it is written
 * in three places -- the world object's constructor (0), the launcher's start-game path (1),
 * and fn_004246b0's own mode==1 branch (2) -- and NEVER written back.
 *
 * GAME_MODE_WORD is -100 until a game mode is picked and 1..n after, written in exactly three
 * places, all inside fn_00429730, and never restored. 0xffffff9c is the image's initial
 * value rather than something the game writes.
 *
 * Both were confirmed by LF2_WATCH over a full route -- launcher, load, mode menu, character
 * select, a match, LEAVE MATCH, and a thousand frames after it -- with the boot transitions as
 * the positive control that the watch fires and silence afterwards.
 *
 * WHAT THAT DOES NOT MEAN, because this comment used to say it did: "so there is no exit
 * sequence to drive". There is. These two words are the OUTER layer -- which program the
 * process is running -- and the game never leaves the game proper once it is in it. Leaving a
 * MATCH is a different layer entirely, SCREEN_WORD below, and the game drives it itself: the
 * post-match overlay's Exit item takes it to screen 10, the front-end menu. The port's exit
 * uses exactly that (runtime/overrides/screens.c), and issue #22 is what happens when the two
 * layers are confused for each other. */
enum { GAME_TOP_MODE = 0x00458b00, GAME_MODE_WORD = 0x0044d070 };

/* SCREEN_WORD is the game's own screen selector, read from the decompilation of fn_0041bc90
 * and fn_00429730 rather than from a .data diff (issue #61). fn_0041bc90 runs the match while
 * it is 0 and otherwise hands it, BY ADDRESS, to fn_00429730, which dispatches on it:
 *
 *      0            the match itself
 *      1            character selection (the panel proper)
 *      2            enter character selection with the stage list armed -> becomes 1
 *      3            reset every player's selection, then -> 1
 *      10           the FRONT-END MENU, fn_00431d10 -- the eight-item list
 *      0x14..0x32   fn_00432ab0        0x78..0x96  fn_00434ab0
 *      200..299     fn_00438b40        300         fn_00437220
 *
 * MENU_CURSOR is the same word the port already knows as the game mode: fn_00429730 passes
 * &0x00451160 to fn_00431d10, which uses it as the front-end cursor (0..7) AND as the mode the
 * chosen item runs in. They are one word on purpose -- picking the item IS picking the mode. */
enum { SCREEN_WORD = 0x0044d020, MENU_CURSOR = 0x00451160 };
enum { SCREEN_MATCH = 0, SCREEN_CHARSELECT = 1, SCREEN_FRONTEND = 10 };
/* THE WHOLE RECORD, from fn_0040c160 -- the bg.dat parser itself (issue #62).
 *
 * Every constant below used to be located by dumping the record and recognising a value
 * (LF2_BG_RECORD, and the derivations are still in assets.c because they were real work).
 * fn_0040c160 makes that unnecessary: it is the function that FILLS this record, one
 * `fscanf` per bg.dat key, so the field a key lands in is written down rather than inferred.
 * Its shape is
 *
 *     void __thiscall FUN_0040c160(this, index, ?, path)
 *         base = this + index * 0x990                     // 0x990 == BG_STRIDE_DW * 4
 *         "name:"        -> base + 0x4d4617c   %s, then every '_' becomes ' '
 *         "width:"       -> base + 0x4d45db0   %d
 *         "zboundary:"   -> base + 0x4d45db4, +0x4d45db8
 *         "perspective:" -> base + 0x4d45dbc, +0x4d45dc0
 *         "shadow:"      -> base + 0x4d46154   %s   then shadowsize %d %d
 *         "layer:"       -> base + 0x4d45dd0 + n*30   %s   (the bitmap path, 30 bytes)
 *           ...the per-layer keys, each  this + (n + index*612)*4 + <constant>
 *
 * The eight constants that were already here came out of it UNCHANGED, which is the check
 * worth stating: an address arrived at by recognising 1500, 300 and 510 in a dump and an
 * address read off the fscanf that writes it are two independent derivations, and they agree
 * to the byte. One name did not survive -- see BG_LAYER_TRANSPARENCY.
 *
 * THE STRINGS ARE THE POINT OF THIS PASS. The record carries the stage's own NAME and each
 * layer's own bitmap PATH, so the port can say "this stage is The Great Wall and its second
 * layer is hill1.bmp" without decrypting bg.dat, without data.txt, and without assuming the
 * load order matches the registry index. That is what lets hand-woven geometry be keyed on
 * the stage's own name and take its depth from a named layer (issue #62). They are BYTES,
 * not dwords, and the layer array's stride is 30 BYTES rather than the 4 the numeric fields
 * use -- indexing it like a dword array reads four layers into one.
 */
enum { BG_STRIDE_DW = 612, BG_MAX_LAYERS = 30 };
enum { BG_NAME_LEN = 30 };              /* the fscanf destinations are 30 bytes apart */
enum { BG_LAYER_TRANSPARENCY = 81027484, /* -120 bg.dat's `transparency:`. This was called
                                          * BG_LAYER_PIC and described as "the picture handed
                                          * to the draw call" -- located by dump, named by
                                          * guess. fn_0040c160 scans `transparency:` into it.
                                          * The DRAW is unaffected: fn_0041a250 passes this
                                          * field as its fourth argument either way, and
                                          * background.c passes the same one. Only the name
                                          * was wrong, and a wrong name is what sends the next
                                          * reader looking for a picture handle. */
       BG_LAYER_SPAN   = 81027604,      /* +0   bg.dat's `width:` -- the scroll span */
       BG_LAYER_X      = 81027724,      /* +120 */
       BG_LAYER_Y      = 81027844,      /* +240 -- appears verbatim in fn_0041a250 */
       BG_LAYER_HEIGHT = 81027964,      /* +360 only the colour-fill path reads this */
       BG_LAYER_LOOP   = 81028084,      /* +480 bg.dat's `loop:` -- repeat step, 0 = none */
       BG_LAYER_C1     = 81028204,      /* +600 bg.dat's `c1:` -- first animation frame */
       BG_LAYER_C2     = 81028324,      /* +720 bg.dat's `c2:` -- last animation frame */
       BG_LAYER_CC     = 81028444,      /* +840 bg.dat's `cc:` -- the frame count, 0 = static */
       BG_LAYER_ANIM   = 81028564,      /* +960 the live frame counter, stepped every draw */
       BG_LAYER_TINT   = 81028684,      /* +1080 non-zero: a colour fill, not a picture */
       BG_LAYER_OBJ    = 81028804 };    /* +1200 the object the draw call is made on */
enum { BG_STAGE_WIDTH  = 81026480,      /* -1124, per background, not per layer */
       BG_Z_MIN        = 81026484,      /* -1120 bg.dat's `zboundary:` -- the far edge of */
       BG_Z_MAX        = 81026488,      /* -1116   the walkable floor, and the near edge   */
       /* bg.dat's `perspective:`, two integers. NOT read anywhere else in this port, and it
        * is here because fn_0040c160 parses it and no shipped bg.dat sets it -- which is a
        * fact worth recording rather than a field worth using. A stage that did set it would
        * be the one piece of depth the DATA states, so if a future stage format ever wants
        * one, this is where the game already looks. */
       BG_PERSPECTIVE_A = 81026492,     /* -1116+4 */
       BG_PERSPECTIVE_B = 81026496,
       BG_SHADOW_W     = 81026500,      /* -1104 bg.dat's `shadowsize:` */
       BG_SHADOW_H     = 81026504,      /* -1100 */
       BG_LAYER_COUNT  = 81026508,      /* -1096; fn_0041a250 loops on this count */
       /* ---- the strings, in BYTES ---- */
       BG_LAYER_NAME   = 81026512,      /* -1092 + layer*30: the layer's bitmap path, as
                                         * written in bg.dat -- `bg\sys\gw\hill1.bmp`, with
                                         * the game's own backslashes. 30 layers of 30 bytes
                                         * is 900, and 81026512 + 900 is exactly
                                         * BG_SHADOW_PATH, which is what makes BG_MAX_LAYERS
                                         * 30 a fact about the record and not a guess. */
       BG_SHADOW_PATH  = 81027412,      /* bg.dat's `shadow:` -- the ellipse bitmap's path */
       BG_STAGE_NAME   = 81027452 };    /* bg.dat's `name:`, with every '_' ALREADY TURNED
                                         * INTO A SPACE by fn_0040c160. So the record says
                                         * "The Great Wall" where the file says
                                         * "The_Great_Wall", and anything matching a file
                                         * name against this has to put the underscores
                                         * back. */
enum { BG_CAMERA_X     = 0x00450bc4 };  /* the world camera, in stage coordinates */
/* A SECOND upper bound on the camera, applied by fn_0041b5d0 right after the stage-width one
 * and only when it is non-zero (0x0041bbad..0x0041bbba):
 *
 *     EAX = stage_width;  EAX += -794;  if (target > EAX) target = EAX;
 *     EAX = [0x00450bb0]; if (EAX && target > EAX) target = EAX;
 *
 * That is the stage-mode section lock -- what stops the camera partway along a stage until
 * the section is cleared. Like everything else in this game it is expressed against the 794
 * screen, so a wider view sees straight past it (issue #36). */
enum { BG_CAMERA_LOCK  = 0x00450bb0 };
/* Its pair. fn_00437860 writes both from one stage-data field B: the camera lock is B-794
 * and this is B, the world x a fighter may walk to (claim C024, issue #43). Zero in VS mode,
 * and the game's object clamp tests `0 < walk` before applying it -- so a port that wrote a
 * non-zero value here in VS mode would invent a boundary the game does not have. */
enum { BG_WALK_LOCK    = 0x00450bb4 };
/* The game's screen width, literal in fn_0041a250. Aliased to geom.h's rather than written
 * twice, so the port has ONE 794 and the offline geometry test is testing the same one. */
enum { BG_SCREEN_W     = GEOM_SCREEN_W };

/* One layer field, or 0 when the index is out of range. Reads only; nothing here writes to
 * the game's own table. */
uint32_t bg_layer_field(uint32_t field_const, int layer);
uint32_t bg_stage_field(uint32_t field_const);   /* a per-background field (no layer index) */
int      bg_layer_count(void);          /* the game's own layer count, clamped to BG_MAX_LAYERS */
void     bg_table_report(void);         /* LF2_BG_TABLE=1: the loaded stage's layers */
void     bg_geom_report(void);          /* LF2_STAGE_GEOM=1: what reached the frame */

/* The loaded stage's own name, as bg.dat spells it -- underscores, not the spaces the record
 * holds -- or NULL when no stage is loaded. NULL and "" are different answers and both are
 * returned honestly: "" would be a record whose name field is empty. */
const char *bg_stage_name(void);
/* The index of a named, non-empty background record, using the SAME parsed registry the game
 * draws from. `-2` means the registry has not loaded; `-1` means it loaded but has no exact
 * `<name>` match. The distinction is what lets a diagnostic wait honestly rather than calling
 * a startup race an unknown stage. */
int         bg_stage_index(const char *name);
/* One layer's bitmap, without its directory: `hill1.bmp` out of `bg\sys\gw\hill1.bmp`. NULL
 * for an index the loaded stage does not have. */
const char *bg_layer_name(int layer);

/* The width the layer pass draws into -- the game's 794, or the widescreen composition.
 * background.c owns it; it is out here because a renderer wanting the stage's geometry
 * (issue #30) needs the same number the layers were placed against. */
int      bg_view_width(void);

/* The camera the WORLD IS DRAWN FROM, which is the game's shifted left by half the extra
 * width so a wider view is CENTRED on what the 4:3 view showed rather than extended to the
 * right (issue #39). Equal to the game's own camera at 794. It is a draw-time value on
 * purpose -- writing it back would feed the camera's own easing and drift; background.c says
 * why in full. */
int      bg_draw_camera(void);
void     bg_camera_report(void);   /* LF2_CAMERA=1: was the wide view actually re-centred? */

/* The stage's own shadow: the size bg.dat's `shadowsize:` gives, and an identity for the
 * loaded stage so a learned object can be discarded when the stage changes. Both are read
 * from the background record the same way every other field is. */
void     bg_shadow_size(int *w, int *h);
uint32_t bg_shadow_stage(void);

/* THE WALKABLE FLOOR, from bg.dat's `zboundary:` (issue #32).
 *
 * z IS the screen row a fighter's feet are on -- LF2's depth axis projects straight down the
 * screen, which is why the game can depth-sort on it and why the shadow ellipse lands at
 * y = z. So this pair is not just a movement clamp: it is where the FLOOR IS on the screen,
 * in the game's own coordinates, and therefore which rows are a horizontal surface and which
 * are the backdrop standing behind it.
 *
 * Returns 0 and leaves the outputs alone when no stage is loaded or the pair is not a sane
 * band, because a lighting pass handed a floor from nowhere would light the whole screen as
 * ground. */
int      bg_z_bounds(int *zmin, int *zmax);

#endif
