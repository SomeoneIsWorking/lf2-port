/* Hand-woven stage geometry preserves its authored texture and vertex colour unchanged.
 * Character shading and cast shadows belong to object sprites only. */
#version 450

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in float v_depth;

/* The stage's art, sampled from the texture render.c has ALREADY uploaded (claim C032) rather
 * than from a second copy. u_tint.x is 1 when a texture is bound and 0 when it is not, so the
 * untextured case is a multiply by white rather than a second pipeline. */
layout(set = 2, binding = 0) uniform sampler2D u_src;

layout(set = 3, binding = 0) uniform Material {
    vec4 u_tint;       /* x: 1 when a texture is bound, 0 when it is not. */
} material;

layout(location = 0) out vec4 o_color;

void main()
{
    /* The texture's alpha carries through: the colour key became alpha on upload, so a
     * sprite's transparent pixels stay transparent here and the composite over the game's own
     * layers is correct without a discard. */
    vec4 tex = texture(u_src, v_uv);
    o_color = mix(vec4(1.0), tex, material.u_tint.x) * v_color;
}
