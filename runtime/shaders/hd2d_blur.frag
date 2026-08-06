/* A separable Gaussian, run once horizontally and once vertically.
 *
 * The pass this replaced used successive LINEAR downsamples as its blur, which is a box
 * filter applied twice: cheap, but it leaves the square edges of the box in the result, and
 * on a frame with a hard highlight (a health bar, a white flash) the glow came out as a
 * visible rectangle. A Gaussian has no such artefact, and separating it makes it two
 * nine-tap passes instead of one eighty-one-tap one.
 *
 * The taps are placed BETWEEN texels and read with linear filtering, so nine weights cost
 * five samples -- the standard trick, and the reason the radius can be wide enough to read
 * as light at 1080p without the tap count going up.
 */
#version 450

layout(location = 0) in  vec4 v_color;
layout(location = 1) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_tex;

layout(set = 3, binding = 0) uniform Blur {
    vec4 u_blur;        /* xy: the step, one texel along the axis being blurred. zw: unused */
};

void main(void)
{
    const float w0 = 0.2270270270;
    const float w1 = 0.3162162162;
    const float w2 = 0.0702702703;
    const float o1 = 1.3846153846;
    const float o2 = 3.2307692308;

    vec2 d = u_blur.xy;
    vec3 c = texture(u_tex, v_uv).rgb * w0;
    c += texture(u_tex, v_uv + d * o1).rgb * w1;
    c += texture(u_tex, v_uv - d * o1).rgb * w1;
    c += texture(u_tex, v_uv + d * o2).rgb * w2;
    c += texture(u_tex, v_uv - d * o2).rgb * w2;

    o_color = vec4(c, 1.0);
}
