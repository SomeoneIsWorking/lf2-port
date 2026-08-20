/* The port's own rendering engine (issue #64).
 *
 * WHY THIS EXISTS. The port's renderer was SDL_Render with depth, geometry and lighting bolted
 * on beside it, and three consecutive fixes turned out to be symptoms of that one fact:
 *
 *   - SDL_Render has NO depth attachment (`SDL_render.h` contains "depth" zero times), so
 *     authored geometry needed a second renderer beside the first.
 *   - The two cannot share a texture. Claim C032 said the geometry pass could sample what
 *     render.c had already uploaded; it is FALSIFIED -- a sample through a foreign command
 *     buffer returns rgba(0,0,0,0), measured against three controls. So a stage's art is on
 *     the GPU twice.
 *   - The two can only MEET as a texture, so a set spanning parallax depths costs one
 *     full-screen colour+depth target per occupied gap.
 *   - The key light had to be duplicated, and drifted fifteen degrees before anyone noticed.
 *
 * None of that is a bug in SDL. SDL_Render is a 2D sprite API and this port draws a lit scene
 * with pixel-art sprites in it.
 *
 * WHAT IT IS NOT. Not a rewrite of the port -- the recompiler, the runtime, the overrides and
 * the DirectDraw shim are untouched. Not a redesign of the display list either: `ddraw.c`
 * reaches the renderer through eight call sites, and turning the game's blit stream into an
 * ordered scene is the part that came out of measurement and is right. This replaces what
 * DRAWS that list, not what records it.
 *
 * THE ACCEPTANCE GATE EXISTS ALREADY, which is what makes this safe. `tools/e2e.sh render`
 * diffs the GPU frame against the software compositor, with a dropped-draw arm proving the
 * comparison can fail. The engine has to pass the SAME test against the SAME compositor before
 * it draws anything the old path could not -- so its first version is deliberately a
 * REPRODUCTION, not an improvement. A reimplementation that cannot be diffed against what it
 * replaces is a rewrite.
 */
#ifndef LF2_ENGINE_H
#define LF2_ENGINE_H

#include <stdint.h>

struct SDL_Renderer;
struct SDL_Texture;

/* One quad, in the composition's own pixels -- which is where the game has already placed it.
 *
 * DEPTH IS AN ORDINAL TURNED INTO A NUMBER, not a guess about the scene. The display list is
 * painter-ordered and that order is the game's own answer; the engine maps position-in-list
 * onto 0..1 so that sprites and authored geometry can share ONE depth buffer instead of meeting
 * as a render target. It does not second-guess the order it was given.
 */
typedef struct {
    float    x, y, w, h;        /* destination, composition pixels */
    float    u0, v0, u1, v1;    /* source, 0..1; ignored when there is no texture */
    float    depth;             /* 0 nearest .. 1 farthest -- the PAINTER ORDER, not a distance */
    float    r, g, b, a;        /* tint, or the colour when there is no texture */
    /* The source SHEET, as guest memory. The engine owns the upload and the cache, so both
     * sprites and stage geometry sample the same object -- which is the whole of defect 2
     * above. 0 for a colour fill. */
    uint32_t src_pixels;
    int      sw, sh, spitch;
    int      keyed;
    uint32_t key_lo, key_hi;
    int      blend;             /* 0 opaque, 1 alpha, 2 premultiplied (GDI text tiles) */
    /* A tile already living in host memory rather than in the guest: GDI text the port
     * rasterises itself. ARGB32 WORDS and PREMULTIPLIED, which is what the tile arena holds --
     * the writer has already multiplied the colour by its coverage, so `blend` must be
     * BLEND_PREMUL for these or every glyph edge darkens. Non-NULL takes precedence over
     * src_pixels. */
    const void *host_argb;
    int         host_w, host_h, host_pitch;
    /* ---- the lighting chain (issues #37, #69) ----
     *
     * The engine shades the OBJECTS STANDING IN THE STAGE and nothing else, and "object" is
     * the game's own answer: a sprite with a ground marker (its shadow ellipse) drawn in front
     * of it. `is_object` marks such a quad -- it is drawn again into the character mask and,
     * as its sheared silhouette, into the cast-shadow mask -- and `ground_gy` is the bottom
     * edge of that ellipse in output pixels, which is where the object meets the floor.
     *
     * `ground_cx` is the ellipse's HORIZONTAL centre, and it is the anchor the cast shadow
     * stands under. The sprite quad's own centre is NOT the same point: LF2's frame art carries
     * a per-frame offset inside its rectangle, so the character's feet sit wherever the game
     * drew the ellipse, not at the middle of the frame that happens to hold them (issue #72 --
     * anchoring at q.x + q.w/2 put the shadow up to a sprite-width to the side of the fighter).
     * The ellipse centre is the object's true base and is what the game's own draw used. */
    int      is_object;
    float    ground_gy;
    float    ground_cx;
} EngineQuad;

/* Hand-woven stage geometry, submitted into the SAME pass as the sprites (issue #64).
 *
 * This is the piece the old arrangement could not do at all. A geometry pass and a sprite pass
 * that cannot share a texture can only meet as a render target, so a set spanning parallax
 * depths cost one full-screen colour+depth pair per gap. Here it is a draw call in the middle
 * of the quad stream, into the one depth buffer.
 *
 * `at` is the quad index this geometry is drawn BEFORE -- the position in the game's painter
 * order that the port chose for it, which is the whole content of the placement. The engine
 * gives it the sliver of depth between that quad and the previous one, so it is ordered against
 * the game's layers by the list and against other geometry in the same sliver by its own
 * parallax depth. Interpenetration is a mesh-against-mesh problem and that is exactly the pair
 * that can interpenetrate.
 */
typedef struct {
    const void *v;              /* MeshVertex *, owned by the caller */
    int         n;
    int         at;             /* draw before quad `at` */
    int         camera;
    /* WHERE THE COMPOSITION SITS ON THE SCREEN, as an affine map from stage pixels to clip
     * space. The caller has it already -- it is the same scale and offset every sprite quad
     * gets -- and passing it rather than a viewport size is what stops the geometry sitting
     * correctly in a 794-wide window and sliding out of the stage in any other. */
    float       sx_scale, sx_bias, sy_scale, sy_bias;
} EngineGeom;

/* Bring the engine up on the device the port's `gpu` renderer is already built on (claim C029),
 * so there is one device and one texture pool. Reports WHY not, once, rather than going quiet:
 * an engine that silently does nothing is indistinguishable from a frame with nothing in it. */
int  engine_init(struct SDL_Renderer *r);
int  engine_ready(void);

/* Is the engine the thing that draws? False keeps the SDL_Render path, which stays as the A/B
 * control arm the way LF2_BG_ORIG did for the background override. */
int  engine_enabled(void);

/* Draw `n` quads into an offscreen target `w` x `h` and return it as a texture the caller can
 * present. NULL if the engine is unavailable or n is 0. Owned by the engine, valid until the
 * next call.
 *
 * With the lighting option on (issue #69), the object quads are additionally drawn into a
 * character mask and a cast-shadow mask, and a light pass re-lights the finished picture into
 * a second target -- the returned texture is the LIT frame, and the masks never leave the
 * engine. Pixels outside those masks are not treated as an effect surface. */
struct SDL_Texture *engine_draw(const EngineQuad *q, int n,
                               const EngineGeom *g, int ng, int w, int h);

/* A guest surface was written to, so any cached upload of it is stale. Same contract as
 * render_surface_dirty -- the cache is validated by content hash as well, and this is the cheap
 * path for the case the port already knows about. */
void engine_surface_dirty(uint32_t pixels);

void engine_report(void);       /* LF2_ENGINE_DEBUG=1: what the engine actually drew */
void engine_shutdown(void);

#endif
