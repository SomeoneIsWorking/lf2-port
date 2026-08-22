/* The engine's sprite quad (issue #64).
 *
 * WHY THERE IS A SECOND VERTEX SHADER BESIDE mesh.vert, and why that is not a duplication.
 * The two differ in exactly one thing: how a position becomes clip space. A stage solid is
 * authored in the stage's four axes and does its parallax divide per vertex, because
 * `screen_x = X - camera/depth` is not linear in (X, depth, 1) and no matrix expresses it.
 * A SPRITE is different: the game has already placed it through its own draw call and
 * render.c has transformed that composition-space rectangle into output pixels -- there is
 * nothing left to project. Forcing both through one shader would mean handing sprites a fake
 * depth and a fake camera so the divide cancelled out, which is inventing a projection to
 * undo it again.
 *
 * They share everything that matters: the same device, the same depth buffer, the same
 * texture pool and the same light. That is the whole point of issue #64 -- the old
 * arrangement had two RENDERERS that could not share a texture, not two vertex shaders.
 *
 * THE DEPTH is supplied per quad rather than derived. The display list is painter-ordered and
 * that order is the game's own answer, arrived at by measurement (a fighter's z, C019's ground
 * markers); the engine turns the ordinal into a depth so that sprites and authored geometry
 * can occupy ONE buffer instead of meeting as a render target. It does not second-guess the
 * order.
 */
#version 450

layout(location = 0) in vec2 a_pos;      /* output pixels -- render.c has already placed it */
layout(location = 1) in float a_depth;   /* 0 nearest .. 1 farthest, already normalised */
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_color;    /* a tint, or the fill colour when there is no texture */

layout(set = 1, binding = 0) uniform View {
    vec4 u_view;     /* x: output width. y: output height. zw: unused. */
} view;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;

void main()
{
    v_uv    = a_uv;
    v_color = a_color;
    /* Output pixels to clip space, y down. No half-pixel offset: render.c already applied the
     * one composition-to-output transform, and adding another offset here would move every
     * sprite from its stated destination -- at scale 1, exactly the drift the byte-identity
     * arm of tools/e2e.py background exists to catch. */
    gl_Position = vec4(2.0 * a_pos.x / view.u_view.x - 1.0,
                       1.0 - 2.0 * a_pos.y / view.u_view.y,
                       a_depth, 1.0);
}
