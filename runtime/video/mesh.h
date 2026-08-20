/* The depth-tested geometry pass: hand-woven stage sets, drawn behind the game's sprites.
 *
 * WHY IT IS A SEPARATE PASS AND NOT MORE QUADS IN THE DISPLAY LIST (issues #49, #62). The
 * display list is PAINTER-SORTED, which is exactly right for LF2's sprites -- the game sorts
 * them itself on z and they never interpenetrate -- and exactly wrong for a mesh, which
 * interpenetrates ITSELF the moment it is more than a flat card. SDL_Render has no depth
 * attachment at all (`SDL_render.h` contains the string "depth" zero times), so a mesh cannot
 * be drawn correctly through it in any submission order.
 *
 * WHAT MAKES THIS ADDITIVE RATHER THAN A RENDERER REWRITE, which is what the feasibility
 * estimate in issue #49 assumed it would be:
 *
 *   - `SDL_GetGPURendererDevice` hands back the device the port's own `gpu` renderer is
 *     already built on, so there is ONE device (claim C029).
 *   - That device takes a D32_FLOAT depth-stencil target, so the pass gets a real depth test.
 *   - The finished colour target is wrapped as an ordinary `SDL_Texture` through
 *     `SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER` -- the SAME object, no copy and no
 *     readback -- and the existing display list draws it as one quad (claim C030).
 *
 * So `render.c` is untouched, the software compositor is untouched, and a stage with no
 * authored geometry allocates nothing and submits nothing. That last part is what keeps
 * `tools/e2e.py background`'s byte-identity arm true.
 *
 * WHY A SET NEEDS NO SHARED DEPTH BUFFER WITH THE SPRITES: it is BEHIND all of them. The
 * interpenetration that would need one is mesh-against-mesh, and that is entirely inside this
 * pass. LF2's own painter order goes on placing the fighters, as it always has.
 */
#ifndef LF2_MESH_H
#define LF2_MESH_H

#include <SDL3/SDL.h>

/* One vertex in the STAGE's own terms -- and there are FOUR of them, not three.
 *
 * x, jump height, floor row and parallax depth are INDEPENDENT in LF2 because the game never
 * unified them (issue #62). A fighter's z is used directly as a screen row, so the depth axis
 * projects down the screen at slope 1 (C018); but every object shifts by the camera FLAT
 * whatever its z, while a layer shifts by camera/depth (C031). Those are two different cameras
 * glued together, and no perspective projection reproduces both -- it would have to give a
 * fighter at the near zboundary a different parallax rate from one at the far, and the game
 * gives them the same.
 *
 * So `row` and `depth` are separate channels and MUST STAY SEPARATE. Collapsing them into one
 * axis -- which is the obvious simplification, and wrong -- authors the geometry against a
 * camera the game does not have, and the set slides off the stage as the camera pans.
 *
 * Not screen coordinates: the projection is the pass's (geom_stage_clip), so geometry authored
 * once is correct at every view width and every camera position. */
typedef struct {
    float x;        /* across the stage, in the game's own pixels */
    float jump;     /* LF2's vertical axis; subtracts from the row */
    float row;      /* the floor row this point stands on -- the game's z, C018 */
    float depth;    /* parallax depth, 1.0 = the fighters' plane; 0 = unknown/infinitely far */
    float u, v;     /* into the source texture, 0..1; ignored when no texture is bound */
    float nx, ny, nz;
    float r, g, b, a;
} MeshVertex;

/* Available at all? Reports WHY not, once, rather than going quiet -- a pass that silently
 * does nothing is indistinguishable from a stage with no geometry in it. */
int  mesh_init(SDL_Renderer *r);
int  mesh_ready(void);

/* Draw `n` vertices as triangles into an offscreen target `w` x `h`, depth-tested, and return
 * it as a texture the display list can place. NULL if the pass is unavailable or n is 0.
 *
 * `camera` is the draw-time camera (bg_draw_camera), and `view_w`/`view_h` the composition the
 * geometry is projected into. They are supplied by the caller because the port is where the
 * stage record and the wide-view camera shift already live (issue #39); deriving a second
 * camera here is how two views drift apart. There is no view MATRIX: screen_x = X - camera/depth
 * is not a linear function of (X, depth, 1), so the divide is per vertex. See geom.h.
 *
 * The returned texture is owned by this module and is valid until the next call FOR THAT
 * SLOT -- which is what lets a frame hold several finished passes at once. */
/* THE ART IS THE PASS'S OWN, and it is on the GPU twice. That is not the design anyone would
 * choose and it is not an oversight -- claim C032 was recorded saying the pass could sample the
 * texture render.c has already uploaded, through SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, and it
 * is FALSIFIED. The handle is readable; a sample through it from this pass's own command buffer
 * comes back rgba(0,0,0,0).
 *
 * That was discriminated rather than assumed, against three controls in one run: the same quad
 * with no texture reads white, so the geometry rasterises; a texture uploaded HERE and sampled
 * by the same pipeline with the same UVs reads its two halves correctly, so the sampler and the
 * UVs are right; and neither SDL_FlushRenderer nor drawing the source through SDL_Render first
 * changes anything, so it is not an upload waiting on a flush. SDL's texture is simply not in a
 * state a foreign command buffer can sample.
 *
 * So: mesh_upload gives back a handle the pass owns, and mesh_draw samples that. The cost is
 * one extra copy of a stage's art in VRAM. The alternative -- reaching into SDL's texture --
 * has been measured and does not work.
 *
 * `art` is NULL for flat vertex colour, which is a multiply by white in the shader rather than
 * a second pipeline. */
typedef struct MeshTexture MeshTexture;

/* Upload RGBA8 pixels the pass will own. NULL if the pass is unavailable or the upload failed,
 * and it says which. Free with mesh_texture_free. */
MeshTexture *mesh_upload(const void *rgba, int w, int h);
void         mesh_texture_free(MeshTexture *t);

/* `slot` is WHICH place in the game's painter order this geometry belongs at, and it is a
 * parameter because one composited quad can only go in at one point while a set spans parallax
 * depths that the game's own layers are painted between. The caller runs the pass once per
 * occupied gap and each gap keeps its own live target; a slot out of range is REPORTED and
 * refused, never clamped, because clamping draws a solid at the wrong point in the order and
 * that looks like bad authoring rather than like a pass out of slots. */
SDL_Texture *mesh_draw(int slot, const MeshVertex *v, int n, int w, int h,
                       int camera, int view_w, int view_h, const MeshTexture *art);

/* LF2_MESH_SELFTEST=1: submit two overlapping triangles in the WRONG painter order -- the far
 * one second -- and report whether the near one survived. A depth pass that is not actually
 * testing draws the far one over the near one and looks perfectly fine on any single frame, so
 * this is the case that MUST come out one way and is what makes an ordinary run's silence
 * mean something. Wired into tools/e2e.py mesh. */
void mesh_report(void);

#endif
