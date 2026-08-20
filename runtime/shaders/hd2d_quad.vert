/* The vertex shader for the engine's lighting passes (issue #64).
 *
 * WHY A THIRD VERTEX SHADER, beside quad.vert and mesh.vert. The hd2d fragment shaders
 * (hd2d_character.frag, hd2d_shadow.frag, hd2d_light.frag) were written for SDL_Render's GPU render
 * states, whose vertex stage uses SDL's own varying convention: colour at location 0, uv at
 * location 1. The engine's quad.vert answers the same two questions in the opposite order, so
 * it cannot drive them, and swapping quad.vert's outputs would touch the one pipeline the
 * byte-identity arms of tools/e2e.sh render verify. This is quad.vert's mapping with SDL's
 * convention -- three lines of arithmetic already shared with mesh.vert by design, so the
 * verified pipeline is not moved for a convenience.
 *
 * There is no depth output to speak of: these passes render into targets with no depth
 * attachment, and the fragment shaders read gl_FragCoord, not an interpolated depth.
 */
#version 450

layout(location = 0) in vec2 a_pos;      /* output pixels -- the caller has already placed it */
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;

layout(set = 1, binding = 0) uniform View {
    vec4 u_view;     /* x: target width. y: target height. zw: unused. */
} view;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;

void main()
{
    v_color = a_color;
    v_uv    = a_uv;
    /* The same composition-pixels-to-clip mapping quad.vert uses, with the depth pinned
     * mid-range: there is no depth buffer in these passes, so any in-range value is as good. */
    gl_Position = vec4(2.0 * a_pos.x / view.u_view.x - 1.0,
                       1.0 - 2.0 * a_pos.y / view.u_view.y,
                       0.5, 1.0);
}
