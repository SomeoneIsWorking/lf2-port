/* The character buffer: which pixels are a fighter, and how high off the ground each one is.
 *
 * ONLY THE STAGE'S CHARACTERS ARE DRAWN THROUGH THIS. Not the background layers, not the HUD,
 * not the text, not the letterbox -- the lighting is for the things standing in the field,
 * and everything else comes out of the pass exactly as the game drew it. That is also why
 * this pass is cheap: it is a handful of quads, not a second walk over the whole frame.
 *
 * WHICH DRAWS THOSE ARE is the game's own answer, not a guess from the picture: LF2 draws a
 * shadow ellipse at an object's feet immediately before drawing the object, so a sprite with
 * an ellipse in front of it is an object in the field and one without is not.
 *
 * WHAT THE CHANNELS MEAN:
 *
 *   R  1 where a character put a pixel down. The colour key became alpha on upload, so this
 *      is the sprite's true silhouette -- which is what the lighting builds its normals from.
 *   G  HEIGHT above the object's ground point, in units of the view height. LF2's y axis is
 *      jump height (claim C018), so a fighter in the air really is higher here, and the
 *      lighting can give them more of the sky.
 *
 * The discard is what makes the silhouette real: without it the transparent corners of every
 * sprite would fill in their quad and the lighting would find a rectangle where the fighter
 * is.
 */
#version 450

layout(location = 0) in  vec4 v_color;
layout(location = 1) in  vec2 v_uv;
layout(location = 0) out vec4 o_gbuf;

layout(set = 2, binding = 0) uniform sampler2D u_tex;

layout(set = 3, binding = 0) uniform Geometry {
    /* x: the ground point, in output pixels from the top.  y: 1 / view height.
     * zw: unused.                                                              */
    vec4 u_geom;
};

void main(void)
{
    float a = texture(u_tex, v_uv).a * v_color.a;
    if (a < 0.5) {
        discard;
    }
    /* gl_FragCoord.y is the output pixel, which is the space the ground point is given in,
     * so the subtraction needs no knowledge of the game's own resolution. */
    float height = clamp((u_geom.x - gl_FragCoord.y) * u_geom.y, 0.0, 1.0);
    o_gbuf = vec4(1.0, height, 0.0, 1.0);
}
