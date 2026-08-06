/* The cast-shadow mask: a sprite's silhouette, and nothing else about it.
 *
 * WHY THIS NEEDS A SHADER AT ALL. The mask is drawn by laying the sprite's own quad down on
 * the ground, and SDL multiplies the vertex colour into the texture -- so a white vertex
 * gives `sprite.rgb * a`, which is the sprite's COLOURS at its coverage, not its coverage.
 * The mask came out with the fighter's teal jacket and red trousers in it, and the lighting
 * then read that colour as "how much shadow", making a shadow that was darker under the
 * bright parts of the sprite. There is no fixed-function blend factor that substitutes 1 for
 * the source colour, so the only honest fix is to write the coverage directly.
 *
 * ALPHA-TESTED rather than blended: overlapping quads then overwrite instead of accumulating,
 * so two fighters standing together do not throw a shadow twice as dark as either. The hard
 * edge this leaves is softened afterwards by the blur, which is where a shadow's softness
 * should come from anyway.
 */
#version 450

layout(location = 0) in  vec4 v_color;
layout(location = 1) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_tex;

layout(set = 3, binding = 0) uniform Shadow {
    vec4 u_unused;      /* SDL wants the uniform buffer the shader declares; nothing reads it */
};

void main(void)
{
    float a = texture(u_tex, v_uv).a * v_color.a;
    if (a < 0.5) {
        discard;
    }
    o_color = vec4(1.0, 1.0, 1.0, 1.0);
}
