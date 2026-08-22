/* fn_0041a5a0 -- the stage's object pass, hand-ported.
 *
 * WHY THIS EXISTS. The pass clamps a fighter's name tag into the game's own 794-wide screen at
 * four `MOV r32,0x31a` sites, so in a wide view the tag freezes 184 px early and is left behind
 * by the fighter it names (issue #55, claim C025 -- measured: the tag stops at 785 = 794 - 9 at
 * every view width). 0x31a is an IMMEDIATE in recompiled code, so unlike the walk lock and the
 * camera word there is no address to write; the only way to make that bound follow the view is
 * to own the function. That is the whole reason this file is a port rather than a wrapper.
 *
 * WHAT IT REPLACES, and what that costs: the pass collects every live object, depth-sorts them,
 * and draws each one's shadow, sprite, multiplier label, name tag and effects. It is 636
 * instructions and it WRITES BACK -- the effects loop advances per-effect counters and
 * decrements obj[0x36c] -- so this is not a drawing routine that can be approximated.
 *
 * HOW IT IS ACCEPTED. `tools/e2e.py objects` compares this against fn_0041a5a0__orig at a 794
 * view where bg_view_width() IS 794 and the port must therefore be byte-identical -- in PIXELS
 * and in STATE (.data and the guest heap, which is deterministic to one byte in 106 MB, claim
 * C026). LF2_OBJ_ORIG=1 selects the recompiled body, which is what the gate's control arm uses.
 *
 * THE SOURCE. Written from re/instructions.tsv rather than the decompilation, which is wrong or
 * silent on three things that matter: the function ends `RET 0xc` and takes THREE stack args
 * where Ghidra types two; every fn_0043f010 call has an elided __thiscall receiver; and the
 * second tag variant's string is a stack buffer holding "Com", which Ghidra rendered as a
 * pointer into a bitmap resource.
 */
#include "overrides.h"
#include "world.h"
#include "geom.h"

#include "guest_ops.h"
#include "guest_map.h"
#include "hostwin.h"
#include "shadowcaster.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void fn_0041a5a0__orig(void);
void fn_0043f010(void);          /* __thiscall(sheet); 6 stack args, RET 0x18 */
void fn_0040de30(void);          /* __thiscall(object); 3 stack args */

/* The globals this pass reads, named rather than repeated as hex. */
enum {
    OBJ_TABLE   = 0x194,          /* this + OBJ_TABLE + i*4 -- 400 object pointers */
    OBJ_EXISTS  = 4,              /* this + OBJ_EXISTS + i  -- the in-world byte (claim C001) */
    NAME_TABLE  = 0x0044fcc0,     /* player names, 0xb bytes each, slots 0..9 */
    NAME_MARK   = 0x00450b4c,     /* per-slot dword; -1 means the name is bracketed */
    NAME_TAIL   = 0x00449060,     /* the word appended after a bracketed name */
    GLYPH_SHEET = 0x0044faf4,     /* the font sheet every tag glyph is drawn from */
    EFFECT_SHEET = 0x0044f8fc,    /* blood/impact icon sheet, loaded at 0041ad17/6b/bc */
    GLYPH_SURF  = 0x00455608,     /* the surface those glyphs go to */
    GLYPH_W     = 9,              /* one glyph cell, and the tag's per-character step */
};

/* The shadow's geometry lives in the background record, at offsets Ghidra folds a base into. */
enum {
    BG_REC_PTR   = 0x7d4,         /* this + BG_REC_PTR -- the record array */
    BG_REC_SIZE  = 0x990,
    SHADOW_W     = 0x4d45dc4,
    SHADOW_H     = 0x4d45dc8,
    SHADOW_SHEET = 0x4d4673c,
};

#define OBJ(i) LD32(self + OBJ_TABLE + (uint32_t)(i) * 4)

/* strlen over guest memory. The game recomputes this before every use of the string -- the
 * same `do { c = *p++; } while (c)` appears eleven times -- and the result is all that the
 * recomputation produces, so it is done once here. */
static int gstrlen(uint32_t p)
{
    int n = 0;
    while (LD8(p + (uint32_t)n)) n++;
    return n;
}

/* The game's own signed halving: CDQ; SUB; SAR 1 is a divide by two that rounds toward zero. */
static int32_t half(int32_t v) { return v / 2; }

/* fn_0043f010 in the guest's ABI. The return address is the REAL one from the call site, so a
 * diagnostic that prints the caller (LF2_GLYPH_POS does) keeps telling the truth. */
static void draw_clip(uint32_t ret, uint32_t sheet, int32_t x, int32_t y, int32_t ch,
                      int32_t a, int32_t b, uint32_t surface)
{
    PUSH32(surface);
    PUSH32((uint32_t)b);
    PUSH32((uint32_t)a);
    PUSH32((uint32_t)ch);
    PUSH32((uint32_t)y);
    PUSH32((uint32_t)x);
    PUSH32(ret);
    R(ECX) = sheet;
    fn_0043f010();
}

/* THE CLAMP, and the one line this whole port exists for.
 *
 * The game's is `x = min(max(x, 0), 794 - 9*len)`: keep a name on the screen. The intent is
 * right and is kept; only the width is wrong, because in this port the screen is as wide as the
 * window. The LOW clamp stays at zero and is checked rather than assumed -- screen x is already
 * relative to the shifted camera, so zero is still the left edge of what is drawn. */
static int32_t tag_clamp(int32_t x, int len)
{
    const int32_t right = (int32_t)bg_view_width() - GLYPH_W * len;
    if (x < 0) x = 0;
    if (right < x) x = right;
    return x;
}

/* Draw a string as sheet glyphs, left to right on a 9 px step. `src` is a guest pointer when
 * `guest` is set and a host buffer otherwise -- the game builds the bracketed name on its own
 * stack, which nothing else reads, so building it here changes no guest state. */
static void draw_tag(uint32_t ret, int32_t x, int32_t y, const char *host, uint32_t gsrc, int len)
{
    const uint32_t sheet = LD32(GLYPH_SHEET);
    const uint32_t surf  = LD32(GLYPH_SURF);
    for (int i = 0; i < len; i++) {
        const int32_t ch = host ? (int32_t)(signed char)host[i]
                                : (int32_t)(int8_t)LD8(gsrc + (uint32_t)i);
        draw_clip(ret, sheet, x, y, ch, 1, 0, surf);
        x += GLYPH_W;
    }
}

void fn_0041a5a0(void)
{
    /* LF2_OBJ_ORIG=1 runs the recompiled body instead, which is what the gate's control arm
     * uses. It needs the old arrangement: the shifted camera written into the guest word for
     * the duration of the call and put back afterwards, since the lifted code reads the word. */
    if (getenv("LF2_OBJ_ORIG")) {
        const uint32_t saved = LD32(BG_CAMERA_X);
        int32_t c = bg_draw_camera();
        const char *sk = getenv("LF2_OBJ_SKEW");
        if (sk) c += (int32_t)strtol(sk, NULL, 10);
        ST32(BG_CAMERA_X, (uint32_t)c);
        fn_0041a5a0__orig();
        ST32(BG_CAMERA_X, saved);
        return;
    }

    /* THE CAMERA, once for the whole pass. This is what the wrapper in background.c used to do
     * by writing the shifted value into the guest's camera word around the recompiled body: the
     * shift is a DRAW-time value and must never be written back, because fn_0041b5d0 eases
     * toward its target by a seventh and reads the word back (issue #39). With the function
     * ported there is no body to fool -- every read below simply uses the drawing camera. */
    int32_t cam = bg_draw_camera();
    { const char *skew = getenv("LF2_OBJ_SKEW"); if (skew) cam += (int32_t)strtol(skew, NULL, 10); }

    const uint32_t self = R(ECX);
    const uint32_t arg1 = LD32(R(ESP) + 4);
    const uint32_t arg2 = LD32(R(ESP) + 8);
    /* arg3 at +12 is popped by RET 0xc and never read -- see the header comment. */

    /* ---- collect every object that is in the world (claim C001: the byte at this+4+i) ---- */
    int list[400];
    int n = 0;
    for (int i = 0; i < 400; i++)
        if (LD8(self + OBJ_EXISTS + (uint32_t)i)) list[n++] = i;

    /* ---- depth sort, the game's own bubble sort on obj[0x18], ascending ----
     * Reproduced exactly rather than replaced with a better sort: it is not stable, and two
     * objects at the same depth come out in an order that is a property of THIS algorithm. */
    for (int m = n - 1; m > 0; m--)
        for (int j = 0; j < m; j++) {
            const int a = list[j];
            if ((int32_t)LD32(OBJ(list[j + 1]) + 0x18) < (int32_t)LD32(OBJ(a) + 0x18)) {
                list[j] = list[j + 1];
                list[j + 1] = a;
            }
        }

    const int32_t bg = (int32_t)LD32(BG_INDEX);   /* world.h: which background record is loaded */

    for (int k = 0; k < n; k++) {
        const int idx = list[k];
        uint32_t o = OBJ(idx);
        const int32_t state = (int32_t)LD32(o + 8);
        const int32_t mag = state < 0 ? -state : state;
        const int32_t phase = mag & 3;             /* the game's signed %4 on a value already >= 0 */
        const uint32_t data = LD32(o + 0x368);

        /* ---- the stage's own shadow ellipse ---- */
        const int draws_ellipse = (int32_t)LD32(o + 0x98) >= 0 &&
            LD32(LD32(o + 0x70) * 0x178 + 0x7ac + data) != 0xbbd &&
            LD32(LD32(o + 0x70) * 0x178 + 0x7ac + data) != 0x270d &&
            LD32(data + 0x6f4) != 0xdf && LD32(data + 0x6f4) != 0xe0 &&
            state > -0x46 && phase < 2;
        if (draws_ellipse) {
            const uint32_t rec = LD32(self + BG_REC_PTR) + (uint32_t)bg * BG_REC_SIZE;
            const int32_t sw  = (int32_t)LD32(rec + SHADOW_W);
            const int32_t sh  = (int32_t)LD32(rec + SHADOW_H);
            const uint32_t sheet = LD32(rec + SHADOW_SHEET);
            o = OBJ(idx);
            const int32_t x = (int32_t)LD32(o + 0x1c) - half(sw) + (int32_t)LD32(o + 0x10)
                              - cam;
            const int32_t y = (int32_t)LD32(o + 0x18) - half(sh);
            draw_clip(0x0041a76cu, sheet, x, y, -1, 1, 0, arg1);
        }

        /* ---- the sprite itself ---- */
        if (phase < 2 && (int32_t)LD32(OBJ(idx) + 8) > -0x19) {
            const int type = (int32_t)LD32(data + DATA_TYPE);
            render_shadow_object_begin((int32_t)LD32(OBJ(idx) + 0x18),
                                       type == DATA_TYPE_CHARACTER,
                                       shadowcaster_should_cast(draws_ellipse, type));
            PUSH32(arg2);
            PUSH32(arg1);
            PUSH32((uint32_t)cam);
            PUSH32(0x0041a7a4u);
            R(ECX) = OBJ(idx);
            fn_0040de30();
            render_shadow_object_end();
        }

        /* ---- the multiplier label, "x<n>", drawn only when there is more than one ----
         * NO clamp on this one: the game does not bound it, and adding one here would be a
         * change rather than a port. */
        o = OBJ(idx);
        const int32_t mult = (int32_t)LD32(o + 0x30c);
        if (mult > 1) {
            char lbl[8];
            const int len = snprintf(lbl, sizeof lbl, "x%d", mult % 100);
            const int32_t x = ((int32_t)LD32(o + 0x1c) - (int32_t)((uint32_t)(len * GLYPH_W) >> 1)
                               + (int32_t)LD32(o + 0x10)) - cam;
            const int32_t y = ((int32_t)LD32(o + 0x18)
                               - (int32_t)LD32(LD32(o + 0x70) * 0x178 + 0x7f8 + LD32(o + 0x368)))
                              - 7 + (int32_t)LD32(o + 0x14);
            draw_tag(0x0041a8a2u, x, y, lbl, 0, len);
        }

        /* ---- the name tag: the draw this port exists for ---- */
        o = OBJ(idx);
        if ((idx < 0x14 || ((int32_t)LD32(o + 0x364) != 5 && LD32(LD32(o + 0x368) + 0x6f8) == 0)) &&
            (int32_t)LD32(o + 8) > -0x19) {
            char built[32];
            const char *host = NULL;
            uint32_t gsrc = 0;
            int len;

            if (idx < 10) {
                gsrc = NAME_TABLE + (uint32_t)idx * 0xb;
                len = gstrlen(gsrc);
                if ((int32_t)LD32(NAME_MARK + (uint32_t)idx * 4) == -1) {
                    /* "[" + name + the word at 0x00449060. The game builds this in a stack
                     * buffer nothing else reads, so building it here perturbs no guest state. */
                    int w = 0;
                    built[w++] = '[';
                    for (int i = 0; i < len && w < (int)sizeof built - 3; i++)
                        built[w++] = (char)LD8(gsrc + (uint32_t)i);
                    const uint32_t tail = LD32(NAME_TAIL);
                    if ((tail & 0xff) && w < (int)sizeof built - 2) built[w++] = (char)(tail & 0xff);
                    if (((tail >> 8) & 0xff) && w < (int)sizeof built - 1)
                        built[w++] = (char)((tail >> 8) & 0xff);
                    built[w] = '\0';
                    host = built;
                    gsrc = 0;
                    len = w;
                }
            } else {
                host = "Com";
                len = 3;
            }

            int32_t x = ((int32_t)LD32(o + 0x1c) - (int32_t)((uint32_t)(len * GLYPH_W) >> 1)
                         + (int32_t)LD32(o + 0x10)) - cam;
            const int32_t y = (int32_t)LD32(o + 0x18) + 3;
            x = tag_clamp(x, len);
            draw_tag(0x0041ab26u, x, y, host, gsrc, len);

        } else if (idx > 0x13 && (int32_t)LD32(o + 8) > -0x19 &&
                   LD32(LD32(o + 0x368) + 0x6f8) == 0 && (int32_t)LD32(o + 0x364) == 5 &&
                   ((int32_t)LD32(LD32(o + 0x368) + 0x6f4) < 0x1e ||
                    (int32_t)LD32(LD32(o + 0x368) + 0x6f4) >= 0x32 ||
                    (int32_t)LD32(LD32(o + 0x368) + 0x6f4) == 0x26)) {
            /* The second tag variant. Ghidra renders its string as a pointer into a bitmap
             * resource; the listing writes 0x6d6f43 into a stack buffer, which is "Com". */
            const int len = 3;
            int32_t x = ((int32_t)LD32(o + 0x1c) - (int32_t)((uint32_t)(len * GLYPH_W) >> 1)
                         + (int32_t)LD32(o + 0x10)) - cam;
            const int32_t y = (int32_t)LD32(o + 0x18) + 3;
            x = tag_clamp(x, len);
            draw_tag(0x0041ac61u, x, y, "Com", 0, len);
        }

        /* ---- the effects/icon loop, which WRITES BACK ----
         * This is the part that makes the port risky rather than tedious: it advances a counter
         * per live effect and drops obj[0x36c] when the last one expires. The four categories
         * differ only in their clip index and their offset from the object. */
        o = OBJ(idx);
        int32_t live = (int32_t)LD32(o + 0x36c);
        if (live > 0) {
            uint32_t slot = 0x3c0;
            int e = 0;
            do {
                uint32_t obj = OBJ(idx);
                const int32_t v = (int32_t)LD32(obj + slot);
                int32_t clip = -1, dx = 0, dy = 0;

                if (v < 5)                       { clip = v;                    dx = -0x33; dy = -0x28; }
                else if (v > 9 && v < 0xf)       { clip = v - 5;                dx = -0x1e; dy = -0x18; }
                else if (v >= 0x14 && v <= 0x1c) { clip = (v - 0x14) / 2 + 10;  dx = -0x33; dy = -0x28; }
                else if (v >= 0x1e && v <= 0x26) { clip = (v - 0x1e) / 2 + 0xf; dx = -0x1e; dy = -0x18; }

                if (clip >= 0) {
                    const int32_t x = ((int32_t)LD32(obj - 0x50 + slot) + (int32_t)LD32(obj + 0x1c))
                                      - cam + dx;
                    const int32_t y = (int32_t)LD32(obj - 0x28 + slot) + dy;
                    draw_clip(0x0041ad22u, LD32(EFFECT_SHEET), x, y, clip, 1, 0, arg1);
                    obj = OBJ(idx);
                    ST32(obj + slot, LD32(obj + slot) + 1);
                } else if (e == live - 1) {
                    obj = OBJ(idx);
                    ST32(obj + 0x36c, (uint32_t)((int32_t)LD32(obj + 0x36c) - 1));
                }

                live = (int32_t)LD32(OBJ(idx) + 0x36c);
                e++;
                slot += 4;
            } while (e < live);
        }
    }

    R(ESP) += 4 + 12;             /* RET 0xc: the return address and three stack args */
}
