/* Depth of field, as a function of DISTANCE and nothing else (issue #63).
 *
 * WHY THE PREVIOUS ONE WAS DELETED, which is the whole design brief. A bloom, a depth of field,
 * atmospheric haze, a vignette and a colour grade all shipped together once and were all cut:
 * each touched every pixel, and together they read as a filter over a screenshot rather than as
 * a remaster. The recorded conclusion is not "do it more carefully" -- it is that a screen-wide
 * effect with no geometry under it HAS NOTHING TO BE RIGHT ABOUT. There is no answer to "is this
 * blur correct" when the blur is a function of screen position.
 *
 * So this one is a function of the G-buffer's distance channel, which is the port's own measured
 * quantity: a background layer's depth is derivable from its parallax rate (claim C031), a solid
 * carries its own, and a sprite carries NONE. Every number here can be checked against the
 * stage's own bg.dat, and `tools/e2e.sh render` asserts the two halves that matter -- it must
 * change a frame with a stage in it, and change NOTHING on a frame without one.
 *
 * WHAT IS DELIBERATELY NOT DONE:
 *   - no tilt-shift, no blur by row, nothing that reads gl_FragCoord as a distance. That is the
 *     version that looks right on one screenshot of one stage and is wrong the moment the
 *     camera pans.
 *   - the FIGHTERS ARE NEVER BLURRED, and not as a special case: they write no distance at all,
 *     so they take the untouched branch by the same rule the HUD does. The focal plane is where
 *     the fight is, which is the one thing a viewer is looking at.
 *
 * THE FOCUS IS 1/d, NOT d. Defocus is a difference of reciprocals, and the reciprocal is what
 * makes the measure bounded and well behaved: the shipped stages run from about 0.89 (a
 * foreground strip, in front of the fighters) to 535 (a distant sky), and 1/d maps all of that
 * into (0, 1.1] with the fighters' plane at exactly 1. Using d directly would put almost the
 * whole range in the last few percent of the blur and make every stage look identical.
 */
#version 450

layout(location = 0) in  vec4 v_color;
layout(location = 1) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_frame;   /* the finished picture */
layout(set = 2, binding = 1) uniform sampler2D u_gbuf;    /* rgb normal, a = distance */

layout(set = 3, binding = 0) uniform Dof {
    vec4 u_params;   /* x: one texel across. y: one texel down.
                        z: max blur radius in texels. w: 1 to run, 0 to pass straight through */
    vec4 u_focus;    /* x: the distance that is in focus. yzw: unused. */
};

void main(void)
{
    vec4 here = texture(u_frame, v_uv);
    float d = texture(u_gbuf, v_uv).a;

    /* NO DISTANCE, NO OPINION. Sprites, the HUD, the text, the port's own UI and every pixel no
     * quad covered all arrive here with d == 0, and they leave exactly as they came. This single
     * branch is what makes the "changes nothing on a frame with no stage in it" arm true by
     * construction rather than by tuning -- a menu frame has no layers, so every pixel takes it. */
    if (u_params.w < 0.5 || d <= 0.0) { o_color = here; return; }

    /* The circle of confusion, from the difference of reciprocals. Clamped to 1 so a very
     * distant sky does not ask for a radius the tap pattern below cannot deliver. */
    float coc = clamp(abs(1.0 / u_focus.x - 1.0 / d), 0.0, 1.0);
    float r = coc * u_params.z;
    if (r < 0.75) { o_color = here; return; }   /* under a texel: nothing to gather */

    /* A ring of taps rather than a box: at these radii (a few texels) a box reads as a smear
     * along the axes, and a ring keeps the blur round, which is what a lens does. Two rings,
     * offset from each other, so the pattern does not show as spokes on a hard edge. */
    const vec2 RING[12] = vec2[12](
        vec2( 1.000,  0.000), vec2( 0.500,  0.866), vec2(-0.500,  0.866),
        vec2(-1.000,  0.000), vec2(-0.500, -0.866), vec2( 0.500, -0.866),
        vec2( 0.866,  0.500), vec2( 0.000,  1.000), vec2(-0.866,  0.500),
        vec2(-0.866, -0.500), vec2( 0.000, -1.000), vec2( 0.866, -0.500));

    vec4 sum = here;
    float wsum = 1.0;
    for (int i = 0; i < 12; i++) {
        /* The outer ring is at r, the inner at half of it -- twelve taps at two radii, which is
         * enough to hide the pattern at the radii this ever reaches. */
        float rr = (i < 6) ? r : r * 0.5;
        vec2 uv = v_uv + RING[i] * rr * u_params.xy;
        float dn = texture(u_gbuf, uv).a;

        /* A TAP ONLY COUNTS IF IT IS AT THE SAME DISTANCE. This is what stops a blurred sky
         * pulling the colour of a sharp fighter standing in front of it out across its own
         * silhouette -- the classic depth-of-field halo, and the thing that would make this read
         * as a filter again. A neighbour with no distance at all (a sprite) is rejected by the
         * same test, so a fighter never bleeds into the layer behind them.
         *
         * The comparison is a RATIO, not a difference, because distance is a scale: 1.0 against
         * 1.2 is a real step between layers, while 500 against 535 is the same sky. */
        float ratio = (dn > 0.0) ? max(d, dn) / min(d, dn) : 1e9;
        if (ratio > 1.05) continue;
        sum += texture(u_frame, uv);
        wsum += 1.0;
    }
    o_color = (sum / wsum) * v_color;
}
