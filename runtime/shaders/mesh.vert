/* The hand-woven stage geometry, projected into the SAME picture the game's own layers land in.
 *
 * WHAT THIS IS FOR (issue #62). LF2's stages are flat painted layers, so the HD2D pass has
 * nothing with real depth in it to light. This is the vertex half of the pass that draws
 * authored geometry BEHIND the sprites -- a set for the fighters to stand in.
 *
 * THE PROJECTION IS NOT INVENTED, and that is the whole of why this can be correct rather than
 * merely plausible. LF2 is a 2.5D field with three real axes -- x across the stage, y up (the
 * jump axis), z into the depth -- and the game already projects them: a background layer's
 * parallax rate IS a perspective divide written as a scroll ratio, and bg.dat's `zboundary:`
 * gives where the walkable floor's near and far edges fall on screen (claim C021, validated on
 * 12 of 12 stages). So the camera here is built from numbers the stage data already carries,
 * and geometry authored against them lands where the game's own layers do.
 *
 * u_view maps the stage's (x, y, z) to clip space. It is supplied by the port rather than
 * derived here, because the port is where the stage record is read and where the camera's own
 * shift for a wide view already lives (issue #39) -- deriving it twice is how two views drift
 * apart.
 *
 * DEPTH IS REAL HERE, and it is the reason this pass exists at all rather than being more
 * quads in the display list. The display list is painter-sorted, which is correct for sprites
 * (LF2 sorts them, and they never interpenetrate) and wrong for a mesh, which interpenetrates
 * itself. gl_Position.z carries into a D32_FLOAT depth attachment and the pipeline tests
 * against it (claim C029).
 */
#version 450

layout(location = 0) in vec3 a_pos;      /* the stage's own axes: x across, y up, z into depth */
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_color;

layout(set = 1, binding = 0) uniform Camera {
    mat4 u_view;                          /* stage space -> clip space, supplied by the port */
} cam;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec4 v_color;
layout(location = 2) out vec3 v_world;

void main()
{
    /* The normal is passed through UNTRANSFORMED because u_view is the only transform and the
     * geometry is authored in stage space: there is no per-object model matrix to rotate a
     * normal by. When there is one, it belongs here with its own inverse-transpose -- not
     * approximated by reusing u_view, which is not a rotation. */
    v_normal = a_normal;
    v_color  = a_color;
    v_world  = a_pos;
    gl_Position = cam.u_view * vec4(a_pos, 1.0);
}
