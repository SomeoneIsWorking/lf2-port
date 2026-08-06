/* The lighting pass: one key light, in the stage's own three axes, on the characters only.
 *
 * WHAT MAKES THIS "ISOMETRIC" RATHER THAN A SCREEN FILTER. LF2 is a 2.5D field -- x across,
 * y up (jump height), z into the depth -- and the game itself sorts on z. The key light here
 * is a DIRECTION IN THAT SPACE, and it is the same vector the cast shadows are sheared
 * along: a fighter's shadow falls where the light says it should, and the shading on the
 * fighter agrees with it. Change the light and both move together. There is no separate
 * "shadow direction" constant anywhere.
 *
 * WHERE THE NORMALS COME FROM, since a sprite has none. A sprite is a billboard standing
 * upright in the field, so its surface faces the camera almost everywhere -- it is only at
 * the SILHOUETTE that it turns away. The silhouette is exactly what the character mask has
 * an edge in, so the normal is built from the gradient of that mask, softened over a few
 * pixels. That bends the normal outward around the edge of a fighter and leaves it facing
 * the camera in the middle: a bevel, which is what gives flat pixel art a side to it.
 *
 * This is honest about what it is not. It is not a normal map, and it cannot know that a
 * character's arm is in front of their chest. The interior shading stays the artist's, which
 * is the right answer for pixel art -- relighting the inside of a sprite would fight the
 * shading that is already painted into it.
 *
 * WHAT IS DELIBERATELY LEFT ALONE, which is nearly everything: the background layers, the
 * HUD, the text and the letterbox are not in the character mask and come out of here as the
 * exact pixels the game composed. The only thing that touches them is the CAST SHADOW, and
 * that is the point of a cast shadow -- it falls on the floor, not on the fighter.
 */
#version 450

layout(location = 0) in  vec4 v_color;
layout(location = 1) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_albedo;
layout(set = 2, binding = 1) uniform sampler2D u_chars;
layout(set = 2, binding = 2) uniform sampler2D u_shadow;

layout(set = 3, binding = 0) uniform Light {
    vec4 u_sun_dir;     /* xyz: toward the key light, in stage axes.  w: key intensity   */
    vec4 u_sun_color;   /* rgb: key colour.                           w: ambient level   */
    vec4 u_sky;         /* rgb: light from above.                     w: bevel strength  */
    vec4 u_bounce;      /* rgb: light bounced off the floor.          w: shadow strength */
    vec4 u_params;      /* xy: one texel.  z: bevel radius in texels. w: height gain     */
    vec4 u_floor;       /* x: the floor's near edge in output rows.   y: 1/feather.
                           z: 1 when the stage said where its floor is, 0 when it did not. */
};

/* The luminance a lighting term carries, so an orientation can change a surface's COLOUR
 * without changing how bright the game's art is. */
float lum(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

/* The softened silhouette the normal is built from. Sampling the mask on a ring rather than
 * at the four neighbours is what gives the bevel a width: a one-texel difference would put a
 * one-pixel rim on a 1920-wide frame and read as an outline drawn round the fighter. */
vec2 mask_gradient(vec2 uv, float r)
{
    vec2 t = u_params.xy * r;
    float l  = texture(u_chars, uv + vec2(-t.x,  0.0)).r;
    float rr = texture(u_chars, uv + vec2( t.x,  0.0)).r;
    float u  = texture(u_chars, uv + vec2( 0.0, -t.y)).r;
    float d  = texture(u_chars, uv + vec2( 0.0,  t.y)).r;
    float tl = texture(u_chars, uv + vec2(-t.x, -t.y)).r;
    float tr = texture(u_chars, uv + vec2( t.x, -t.y)).r;
    float bl = texture(u_chars, uv + vec2(-t.x,  t.y)).r;
    float br = texture(u_chars, uv + vec2( t.x,  t.y)).r;
    /* Sobel, divided by its own maximum so a step from 0 to 1 gives exactly 1. Without that
     * normalisation the raw Sobel of a binary mask comes out at 4, the normal is tipped very
     * nearly into the screen plane at every silhouette, and the shading reads as a dark
     * OUTLINE drawn round the fighter rather than as a surface turning away. It points INTO
     * the fighter, so the negation below is what makes it face outward. */
    return vec2((tr + 2.0 * rr + br) - (tl + 2.0 * l + bl),
                (bl + 2.0 * d  + br) - (tl + 2.0 * u + tr)) * 0.25;
}

void main(void)
{
    vec3 albedo = texture(u_albedo, v_uv).rgb * v_color.rgb;
    vec4 g = texture(u_chars, v_uv);

    float mask   = g.r;                 /* 1 on a character */
    float height = g.g;                 /* above its ground point, in view heights */

    /* Two rings: a wide one for the body of the bevel and a tight one for the edge itself,
     * so the shading is strongest at the silhouette and fades inward. */
    vec2 grad = mask_gradient(v_uv, u_params.z) + mask_gradient(v_uv, u_params.z * 0.4);
    grad *= u_sky.w;

    /* Stage axes: x across the screen, y up, z toward the camera. The screen's y runs down,
     * hence the negation. The z term is what keeps the middle of the sprite facing us. */
    vec3 n = normalize(vec3(-grad.x, grad.y, 1.0));

    vec3 L = normalize(u_sun_dir.xyz);
    /* What a surface facing the CAMERA gets. That is what the stage's backdrop is, and what
     * the whole picture used to be treated as. */
    float wrap_flat = clamp(L.z * 0.5 + 0.5, 0.0, 1.0);
    float ndl_flat  = wrap_flat * wrap_flat;
    /* WRAPPED diffuse, not clamped Lambert. A hard `max(dot, 0)` sends the whole away-facing
     * side of the silhouette to ambient only, which on a 32-pixel sprite magnified to 1080p
     * is a dark line round one side of the fighter. Wrapping the cosine into 0..1 and
     * squaring it keeps the contrast where the light actually falls off and leaves the shaded
     * side lit -- which is also what a real scene does, since a fighter's dark side still
     * sees the sky. */
    float wrap = clamp(dot(n, L) * 0.5 + 0.5, 0.0, 1.0);
    float ndl = wrap * wrap;

    /* The cast-shadow mask, drawn from the fighters' own silhouettes and blurred. */
    float shade = 1.0 - u_bounce.w * clamp(texture(u_shadow, v_uv).r, 0.0, 1.0);

    /* A hemisphere ambient: what a surface sees is the sky above and the floor below, in a
     * proportion that depends on which way it faces. A fighter off the ground sees more sky,
     * which is the cue that sells a jump. */
    float up = clamp(n.y * 0.5 + 0.5 + height * u_params.w, 0.0, 1.0);
    vec3 ambient = mix(u_bounce.rgb, u_sky.rgb, up) * u_sun_color.w;

    vec3 character = albedo * (ambient + u_sun_color.rgb * (u_sun_dir.w * ndl * shade));

    /* ---- the stage: a floor is a HORIZONTAL surface, a backdrop is a vertical one ----
     *
     * Which rows are which is the stage's own answer and not a guess from the picture: LF2's
     * depth axis projects straight down the screen -- that is why the game depth-sorts on z
     * and why it puts the shadow ellipse at y = z -- so bg.dat's `zboundary:` is literally
     * where the floor begins on the screen (issue #32, BG_Z_MIN in world.h). Above it a
     * fighter cannot walk, because it is a wall.
     *
     * The two orientations see different amounts of sky: a floor faces straight up and takes
     * all of it, a backdrop faces the camera and takes half. That is the only difference
     * applied, and it is applied AS COLOUR ONLY -- both terms are normalised to the same
     * luminance first.
     *
     * The luminance lock is the point, not a caution. The art is already painted with its own
     * light in it; re-lighting the brightness of a whole stage is how a pass stops being
     * geometry and becomes a filter, which is exactly what the version before this one did.
     * What survives is the cue that is genuinely geometric: the ground picks up the sky's
     * colour and the rock face does not.
     *
     * With no floor band from the stage (u_floor.z == 0) this collapses to the backdrop term
     * everywhere, so a stage the port could not read comes out as the game drew it. */
    float floorness = u_floor.z
                    * clamp((gl_FragCoord.y - u_floor.x) * u_floor.y + 0.5, 0.0, 1.0);

    vec3 flat_face = mix(u_bounce.rgb, u_sky.rgb, 0.5) * u_sun_color.w
                   + u_sun_color.rgb * (u_sun_dir.w * ndl_flat);
    vec3 up_face   = u_sky.rgb * u_sun_color.w
                   + u_sun_color.rgb * (u_sun_dir.w * L.y * L.y);

    /* The RATIO of the two, each at unit luminance -- so what is applied is purely the hue
     * difference between facing up and facing the camera, and the backdrop's own multiplier
     * is EXACTLY 1. That exactness matters: it is what lets the pass promise that a frame
     * with no floor and no fighters in it comes out byte-identical, which is the arm of
     * ctest render that would catch this spreading over the whole picture. */
    vec3 tint = mix(vec3(1.0),
                    (up_face / max(lum(up_face), 1.0e-4))
                        * (max(lum(flat_face), 1.0e-4) / max(flat_face, vec3(1.0e-4))),
                    floorness);

    vec3 ground = albedo * tint * shade;

    o_color = vec4(mix(ground, character, mask), 1.0);
}
