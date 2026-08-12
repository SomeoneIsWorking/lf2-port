/* The engine's sprite quad, and it deliberately does NOTHING but sample (issue #64).
 *
 * WHY THERE IS NO LIGHTING HERE, when the whole reason for a new engine is to light properly.
 * The engine has to be shown to draw what the renderer it replaces draws before it is allowed
 * to draw anything better: `tools/e2e.sh render` diffs the GPU frame against the software
 * compositor to within a level or two of 255, and that comparison is the only reason any of
 * this can be believed. A first version that both replaced the renderer AND changed the shading
 * would fail that test for two reasons at once and could not be told apart from a broken one.
 *
 * So this reproduces the old path exactly: sample, multiply by the vertex colour, out. The
 * lighting moves in afterwards, as its own step, against the same test -- and when it does it
 * will be REAL shading with the depth buffer this engine has, rather than hd2d.c's reconstruction
 * of a normal from the gradient of a finished picture's silhouette.
 *
 * u_flags.x is 1 when a texture is bound and 0 when it is not, so a colour fill is a multiply
 * by white rather than a second pipeline -- the same arrangement mesh.frag uses.
 */
#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in float v_world;

layout(set = 2, binding = 0) uniform sampler2D u_src;

layout(set = 3, binding = 0) uniform Flags {
    vec4 u_flags;      /* x: 1 with a texture, 0 without. yzw: unused. */
} f;

layout(location = 0) out vec4 o_color;
/* THE G-BUFFER (issue #63). rgb is a surface normal in the stage's own axes; a is the draw's
 * real DISTANCE as a parallax depth, 1.0 being the plane the fighters stand in.
 *
 * A sprite writes a normal of exactly (0,0,0), which is not a unit vector and so can never be a
 * real one -- that is the marker, and it is exact rather than a tolerance. A sprite HAS no
 * normal: it is flat art, and the bevel the lighting builds from its silhouette is a deliberate
 * reconstruction, not something this pass should pretend to know. Anything reading this buffer
 * therefore tests `length(n) > 0.5` to ask "is there a real surface here", and gets a truthful
 * no for every sprite in the frame. */
layout(location = 1) out vec4 o_gbuf;

void main()
{
    /* The texture's alpha carries through untouched. The colour key became alpha on upload --
     * that conversion is where this port's blend stage comes from -- so a sprite's keyed pixels
     * are already transparent by the time they arrive and no discard is needed. A discard would
     * also disable early depth testing, which is the one thing this engine has that the path it
     * replaces could not have at all. */
    vec4 tex = texture(u_src, v_uv);
    o_color = mix(vec4(1.0), tex, f.u_flags.x) * v_color;
    o_gbuf  = vec4(0.0, 0.0, 0.0, v_world);
}
