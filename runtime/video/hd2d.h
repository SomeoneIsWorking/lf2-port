/* Isometric lighting and sprite-cast shadows: THE LIGHT RIG, and nothing else.
 *
 * WHAT THIS FILE IS NOW. The shading itself -- the character buffer, the cast-shadow mask and
 * the light pass over the finished frame -- moved INTO the port's own engine
 * (runtime/video/engine.c), as real SDL_GPU passes running the same committed shaders. The
 * version before that bolted the effect onto SDL_Render with GPU render states, which is the
 * arrangement issue #64 replaced and issue #69 removed. What is left here is the part that was
 * never the problem and must stay single:
 *
 *   THE LIGHT        ONE direction, in the stage's own axes, derived from the two angles the
 *                    pause menu sets. The engine's shading pass, its cast-shadow shear and the
 *                    stage geometry's fragment shader ALL read it from here -- a second copy
 *                    is the exact bug this file exists to prevent (mesh.c once held one,
 *                    fifteen degrees away, under a comment saying they matched).
 *   THE LOOK         the six-vec4 uniform block the light shader is fed, filled by
 *                    hd2d_light_uniforms, so the constants are written down once.
 *
 * There is deliberately no approximation and no fallback in here: a lighting pass that cannot
 * run says so (engine.c reports it) and the picture is the plain composition. The version
 * before shaders approximated a bright pass with SDL_BLENDMODE_MOD, and an approximation that
 * runs when the real thing cannot is how that survived as long as it did.
 */
#ifndef LF2_HD2D_H
#define LF2_HD2D_H

/* ---- the light rig ----
 *
 * ONE direction, in the stage's axes: x across, y up, z toward the camera. It lives in
 * hd2d.c; the engine's shaders shade with it, and this is the only part of it anything
 * outside needs.
 *
 * WHERE A POINT AT HEIGHT 1 LANDS ON THE GROUND, in screen units, relative to the point it
 * is above. This is the whole of a directional light's shadow projection, and both numbers
 * come from that one vector -- so a shadow's DIRECTION, its LENGTH, and how a jump displaces
 * it all follow the light together, and none of them can be given a different one.
 *
 *   *across  = -Lx/Ly   how far sideways: which way the shadow points
 *   *up      =  Lz/Ly   how far up the screen: how LONG the shadow is, since LF2's depth
 *                       axis projects straight down the screen (claim C021)
 *
 * Both are cot(elevation) scaled by the light's heading, so a light near the horizon throws
 * a long shadow and one overhead throws almost none. */
void hd2d_shadow_project(float *across, float *up);

/* THE LIGHT AS TWO ANGLES, which is how a player thinks about it and how the pause menu's
 * Options screen sets it (issue #37).
 *
 *   AZIMUTH    degrees around the fighters. 0 puts the light straight in front of them,
 *              negative swings it to the left. It is what decides which way a shadow points.
 *   ELEVATION  degrees above the horizon. 90 is straight overhead, and it is what decides how
 *              long a shadow is -- the shear and the airborne offset are both cot(elevation).
 *
 * Setting them recomputes the one direction vector, so the shading and the cast shadows move
 * together and cannot be given different lights. */
void hd2d_light_angles(float *azimuth_deg, float *elevation_deg);
/* The one direction vector those two angles produce, in the stage's own axes (x across, y up
 * = LF2's jump axis, z toward the camera). It is exposed so that the geometry pass can light a
 * hand-woven set from the SAME light rather than keeping a second copy -- see mesh.c, where a
 * copy had already drifted to a different direction while its comment said the two agreed.
 * A set lit from somewhere the fighters' shadows do not come from is the one contradiction
 * this whole subsystem is arranged to prevent. */
void hd2d_light_vector(float out[3]);
void hd2d_light_set_angles(float azimuth_deg, float elevation_deg);

/* ---- the look ----
 *
 * The uniform block hd2d_light.frag is fed, as SIX vec4s (24 floats), filled here so the
 * constants are written down in exactly one place:
 *
 *   [0] u_sun_dir    xyz: toward the key light, in stage axes.  w: key intensity
 *   [1] u_sun_color  rgb: key colour.                           w: ambient level
 *   [2] u_sky        rgb: light from above.                     w: bevel strength
 *   [3] u_bounce     rgb: light bounced off the floor.          w: shadow strength
 *   [4] u_params     xy: one texel (1/w, 1/h). z: bevel radius in texels. w: height gain
 *   [5] u_floor      x: the floor's near edge in output rows.   y: 1/feather.
 *                    z: 1 when the stage said where its floor is, 0 when it did not.
 *
 * `floor_row` is the output row the stage's walkable floor begins at, from bg.dat's own
 * z boundary; `have_floor` is 0 when the stage did not say, in which case the whole picture
 * is lit as a surface facing the camera. The numbers are chosen so a FLAT, unshadowed,
 * camera-facing pixel comes out at very close to the colour the game drew: the light must not
 * be a brightness or a tint applied to the game, and what is visible is the DIFFERENCE from
 * flat -- the bevel round a fighter's silhouette, the sky catching them when they jump, and
 * the shadow they throw. */
void hd2d_light_uniforms(float out[24], int w, int h, float floor_row, int have_floor);

#endif
