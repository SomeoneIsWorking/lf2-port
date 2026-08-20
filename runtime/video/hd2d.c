/* Isometric lighting and sprite-cast shadows -- see runtime/video/hd2d.h for the scope. */

#include "hd2d.h"
#include "stagelight.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the light rig ----
 *
 * The key light, as a direction in the stage's three axes: x across the screen, y up (LF2's
 * jump axis, claim C018), z toward the camera. Overhead, leaning slightly left and a little
 * in front of the fighters.
 *
 * It used to be (-0.55, 0.74, 0.38), which is a low SIDE light: it threw long shadows well
 * out to the right and lit one whole flank of a fighter while leaving the other on ambient.
 * A high key is the conventional one for this kind of art -- the shadow tucks in close under
 * the fighter and the shading reads as form rather than as a direction.
 *
 * This is the ONE place a light direction is written down. The engine's shading pass, its
 * cast-shadow shear and the stage geometry's fragment shader all read it from here.
 */
/* DERIVED from the default angles rather than written out, because a vector default is a
 * second spelling of the same fact and the two had already drifted (see stagelight.h). Filled
 * lazily because mesh_init may ask for the light before hd2d has been initialised. */
static float LIGHT[3];
static int   light_ready;

/* The same direction as the two angles a player sets it with. Kept beside the vector rather
 * than derived back out of it, because going back is ambiguous at the poles and the menu
 * would jitter as it rounded. */
static float light_az = STAGELIGHT_AZ_DEFAULT, light_el = STAGELIGHT_EL_DEFAULT;

static void light_ensure(void)
{
    if (light_ready) return;
    light_ready = 1;
    /* LF2_HD2D_LIGHT=<azimuth>,<elevation> in degrees. A DIAGNOSTIC: the light is the
     * player's, set from the pause menu's Options screen, and this exists so a test can put
     * it somewhere known and check that the shadows actually followed. "The shape responds to
     * the light" is not something a single screenshot can show. Read HERE rather than in an
     * init function because the port no longer has an hd2d init for this to hang off -- the
     * light is the part that outlived the SDL_Render chain, and any reader may be the first. */
    {
        const char *v = getenv("LF2_HD2D_LIGHT");
        if (v) {
            float az = light_az, el = light_el;
            if (sscanf(v, "%f,%f", &az, &el) == 2) {
                hd2d_light_set_angles(az, el);
                fprintf(stderr, "hd2d: LF2_HD2D_LIGHT put the key at azimuth %.0f, elevation "
                                "%.0f\n", (double)light_az, (double)light_el);
            } else {
                fprintf(stderr, "hd2d: LF2_HD2D_LIGHT=%s is not <azimuth>,<elevation> -- the "
                                "light is UNCHANGED at %.0f,%.0f\n",
                        v, (double)light_az, (double)light_el);
            }
        }
    }
    stagelight_vector(light_az, light_el, LIGHT);
}

void hd2d_light_angles(float *az, float *el) { *az = light_az; *el = light_el; }
void hd2d_light_vector(float out[3])
{
    light_ensure();
    out[0] = LIGHT[0]; out[1] = LIGHT[1]; out[2] = LIGHT[2];
}

void hd2d_light_set_angles(float az, float el)
{
    /* The clamp, the wrap and the conversion are all stagelight.h's, INCLUDED rather than
     * copied -- ctest stagelight walks them offline, which is the only way anything about this
     * light could be asserted without booting the game and looking at a picture. */
    light_az = stagelight_wrap_azimuth(az);
    light_el = stagelight_clamp_elevation(el);
    stagelight_vector(light_az, light_el, LIGHT);
    light_ready = 1;
}

/* The shadow projection -- see hd2d.h. A point at height h above the ground casts to
 * h * (-Lx/Ly, -Lz/Ly) in the stage's own axes, and LF2's z projects straight down the
 * screen, so the second term is a screen-Y displacement UP the picture. Dividing by Ly is
 * what makes a low light throw a long shadow and a high one throw a short one -- the same
 * relationship the shading has, from the same vector. */
void hd2d_shadow_project(float *across, float *up)
{
    light_ensure();
    stagelight_shadow(LIGHT, across, up);
}

/* ---- the look ----
 *
 * The numbers are chosen so that a FLAT, unshadowed, camera-facing pixel comes out at very
 * close to the colour the game drew -- ambient*hemisphere + key*n.L with the normal facing
 * the camera sums to about 1.0. That is deliberate, and it is the discipline of this pass:
 * the light must not be a brightness or a tint applied to the game. What is visible is the
 * DIFFERENCE from flat -- the bevel round a fighter's silhouette, the sky catching them when
 * they jump, and the shadow they throw.
 *
 * LF2_HD2D_* are for sweeping these while tuning, not configuration. */
static float knob(const char *name, float dflt)
{
    const char *v = getenv(name);
    return v ? (float)atof(v) : dflt;
}

void hd2d_light_uniforms(float out[20], int w, int h)
{
    /* The lazy fill, and this is the site that made it necessary to be careful: LIGHT is no
     * longer a literal initialiser, so a pass that read it before anything had filled it would
     * get (0,0,0) and light every fighter from nowhere. Every reader goes through this. */
    light_ensure();
    float *u = out;
    /* [0] u_sun_dir -- toward the key light, in stage axes. w: key intensity */
    u[0] = LIGHT[0]; u[1] = LIGHT[1]; u[2] = LIGHT[2]; u[3] = knob("LF2_HD2D_KEY", 1.48f);
    /* [1] u_sun_color -- a warm key against a cool sky is what puts a temperature difference
     * between the lit side and the shaded side of a fighter, which is what makes flat art read
     * as having a form rather than just a brightness. */
    u[4] = 1.10f; u[5] = 1.02f; u[6] = 0.90f; u[7] = knob("LF2_HD2D_AMBIENT", 0.66f);
    /* [2] u_sky -- light from above. w: bevel strength */
    u[8] = 0.62f; u[9] = 0.68f; u[10] = 0.80f; u[11] = knob("LF2_HD2D_BEVEL", 0.90f);
    /* [3] u_bounce -- light bounced off the floor. w: shadow strength */
    u[12] = 0.55f; u[13] = 0.52f; u[14] = 0.50f; u[15] = knob("LF2_HD2D_SHADOW", 0.55f);
    /* [4] u_params -- xy: one texel. z: bevel radius in texels. w: height gain */
    u[16] = 1.0f / (float)w; u[17] = 1.0f / (float)h;
    u[18] = knob("LF2_HD2D_BEVEL_PX", 5.0f);
    u[19] = knob("LF2_HD2D_HEIGHT_GAIN", 0.9f);
}
