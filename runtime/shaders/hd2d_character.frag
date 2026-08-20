/* The character buffer: which pixels are a fighter, and how high off the ground each one is.
 *
 * Only stage characters are drawn through this pass. Backgrounds, HUD, text, and letterbox
 * remain exactly as the game drew them. The game's shadow-ellipse/object pairing identifies
 * the eligible quads, and alpha testing yields the sprite's real silhouette rather than its
 * rectangular texture bounds.
 */
#version 450

layout(location = 0) in  vec4 v_color;
layout(location = 1) in  vec2 v_uv;
layout(location = 0) out vec4 o_character;

layout(set = 2, binding = 0) uniform sampler2D u_tex;

layout(set = 3, binding = 0) uniform Geometry {
    vec4 u_geom; /* x: ground row in output pixels. y: 1 / output height. */
};

void main(void)
{
    float a = texture(u_tex, v_uv).a * v_color.a;
    if (a < 0.5) discard;
    float height = clamp((u_geom.x - gl_FragCoord.y) * u_geom.y, 0.0, 1.0);
    o_character = vec4(1.0, height, 0.0, 1.0);
}
