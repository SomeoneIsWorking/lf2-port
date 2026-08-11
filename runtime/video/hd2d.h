/* Isometric lighting and sprite-cast shadows, on the characters standing in the stage.
 *
 * WHY THIS IS A SEPARATE FILE FROM runtime/video/render.c. render.c's job is to turn the game's
 * draws into geometry -- what was drawn, where, from which sheet. What that geometry is then
 * lit by is a different question with a different failure mode, and mixing them is how the
 * first version of this ended up unable to tell "the renderer drew nothing" apart from "the
 * light did nothing". render.c owns the display list and the render targets; this file owns
 * the shaders and the light rig, and the two meet at exactly three points: the character
 * buffer's uniforms, the light direction the cast shadows are sheared along, and hd2d_post.
 *
 * WHAT IT DOES, and nothing else:
 *
 *   CHARACTER BUFFER  which pixels belong to a fighter, and how high each one is off its
 *                     ground point. The game itself says which draws those are -- it puts a
 *                     shadow ellipse at an object's feet immediately before drawing it.
 *   LIGHTING          one key light as a direction in the stage's own axes, a hemisphere
 *                     ambient, and a bevel normal built from the sprite's silhouette. It is
 *                     applied ONLY where the character mask is set: the scenery, the HUD and
 *                     the text come through as the pixels the game composed.
 *   CAST SHADOWS      the sprite's own silhouette laid on the ground, sheared along the same
 *                     light vector, softened, and taken out of the light where it falls.
 *
 * WHAT IT DELIBERATELY DOES NOT DO. An earlier version of this file also had a bloom, a
 * depth of field, an atmospheric haze, a vignette and a colour grade. Every one of them
 * touched every pixel of the frame, and together they read as a filter over a screenshot
 * rather than as light in a scene -- the game came out foggy and washed out. They are gone.
 * A lighting pass whose effect on a frame with no fighters in it is *nothing* is the correct
 * shape for this.
 *
 * IF THE SHADERS CANNOT BE CREATED the port says so once and presents the plain composition.
 * There is deliberately no approximation to fall back to: the version before this one
 * approximated a bright pass with SDL_BLENDMODE_MOD because it had no shader path, and an
 * approximation that runs when the real thing cannot is how that survived as long as it did.
 */
#ifndef LF2_HD2D_H
#define LF2_HD2D_H

struct SDL_Renderer;
struct SDL_Texture;

/* LF2_HD2D=off. A DIAGNOSTIC -- the light is on by default, and this exists so
 * tools/routes/render_test.sh can compare the renderer's geometry against the software compositor
 * with nothing on top of it, and so the pass can be shown to change the frame. */
int  hd2d_wanted(void);

/* Creates the shaders. Returns 0 and explains itself if the renderer has no GPU device or
 * no shader format this port ships. Safe to call repeatedly. */
int  hd2d_init(struct SDL_Renderer *r);
void hd2d_shutdown(void);
int  hd2d_ready(void);          /* the shaders exist and the pass can run */

/* ---- the light rig ----
 *
 * ONE direction, in the stage's axes: x across, y up, z toward the camera. It lives in
 * hd2d.c; the shader shades with it, and this is the only part of it anything outside needs.
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
 * a long shadow and one overhead throws almost none. The version before this used the first
 * of these and a CONSTANT 0.30 in place of the second, which is exactly why moving the light
 * changed where a shadow pointed but never how long it was (issue #38).
 */
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
void hd2d_light_set_angles(float azimuth_deg, float elevation_deg);

/* ---- the character buffer ----
 *
 * render.c draws the stage's objects a second time with this state active, setting the
 * uniforms per quad. begin() returns 0 if the state could not be set, in which case the
 * caller must not draw the pass at all rather than drawing it unshaded.
 */
int  hd2d_chars_begin(float inv_view_height);
void hd2d_chars_quad(float ground_y);
void hd2d_chars_end(void);

/* The cast-shadow mask, drawn the same way: the objects' laid-down quads, with a shader that
 * writes the sprite's COVERAGE rather than its colour. */
int  hd2d_shadow_begin(void);
void hd2d_shadow_end(void);

/* ---- the light ----
 *
 * albedo/chars/shadow are full-resolution targets render.c filled; `out` is where the lit
 * frame goes. `floor_row` is the output row the stage's walkable floor begins at, from
 * bg.dat's own z boundary, and `have_floor` is 0 when the stage did not say -- in which case
 * the whole picture is lit as a surface facing the camera, which is what it was before the
 * floor was located at all. Returns 0 without touching `out` if anything could not be
 * created.
 */
int  hd2d_post(struct SDL_Texture *albedo, struct SDL_Texture *chars,
               struct SDL_Texture *shadow, struct SDL_Texture *out, int w, int h,
               float floor_row, int have_floor);

void hd2d_report(void);         /* LF2_RENDER_DEBUG=1 */

#endif
