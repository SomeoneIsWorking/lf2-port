/* The port's renderer options -- which renderer draws, and whether character lighting runs.
 *
 * WHY THIS EXISTS, and why it is not two getenv calls. The renderer and character lighting were
 * chosen by LF2_ENGINE / LF2_HD2D and nowhere else: a setting a player cannot find
 * is not a feature, which is the project's standing rule for the whole LF2_* namespace
 * (issue #69). The state lives here, the RmlUi settings screen owns it while the game is
 * up, and the consumers -- engine.c, hd2d.c -- READ it rather than caching it, so a change
 * from the menu applies to the next frame.
 *
 * THE ENV VARS ARE STILL HONOURED, ONCE, AS THE INITIAL VALUE. The route scripts under
 * tools/routes pin them to keep every A/B arm meaning exactly what it meant, and a route never
 * opens the settings screen, so an initial value is the whole run's value there. That lets the
 * player's switch and the test's control arm share one implementation without either lying.
 *
 * The RmlUi screen persists both choices through config.c. Environment values remain
 * deterministic route pins for the current run.
 */
#ifndef LF2_OPTIONS_H
#define LF2_OPTIONS_H

#include "spritefilter.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Which renderer draws: the port's own engine (SDL_GPU, with character shading) or the classic
 * SDL_Render path, which stays as the plain-picture fallback and the byte-identity control
 * arm. Default ENGINE. LF2_ENGINE=0 pins classic at startup, any other non-empty value pins
 * the engine. */
int opt_renderer_engine(void);
void opt_set_renderer_engine(int on);

/* The HD2D lighting and its cast shadows. Default ON. LF2_HD2D=off pins it off at startup. */
int opt_lighting(void);
void opt_set_lighting(int on);

/* Authored on-screen controls are enabled by default on touch devices and disappear while
 * a physical gamepad is connected. The persistent setting allows a player to hide them. */
int opt_touch_controls(void);
void opt_set_touch_controls(int on);

/* THE KEY LIGHT'S STRENGTH (issue #111). This is u_sun_dir.w, the multiplier on the lit
 * term of hd2d_light.frag -- 1.0 is a physically flat key, and the shipped look sits at
 * 1.48. The RmlUi GRAPHICS tab owns it as a percentage; config key `light_intensity`.
 * LF2_HD2D_KEY stays honoured once at first read as the route pin, exactly like LF2_HD2D. */
float opt_light_intensity(void);
void opt_set_light_intensity(float v);

/* HOW MAGNIFIED OBJECT SPRITES ARE SAMPLED (issue #112). The frame is drawn at the window's
 * resolution, so a fighter is magnified about twice and plain nearest sampling leaves every
 * edge staircased. A player builds an ordered CHAIN of resampling passes over the art, plus
 * edge smoothing, an inward contour and an exterior outline; runtime/video/spritefilter.h
 * defines what a chain may hold and how it is spelled, and quad.frag evaluates it. Empty --
 * the default -- is the original picture.
 *
 * Config key `sprite_passes`, holding the same string the parser reads, e.g.
 * `nearest:1/2,nearest:2,aa,inner,outline:1`. LF2_SPRITE_PASSES pins it once at first read for
 * route arms (the issue #69 pattern). A spec that does not parse is REFUSED, by name, on
 * stderr -- the chain then stays empty rather than quietly becoming a different chain. */
const SpriteChain *opt_sprite_chain(void);
void opt_set_sprite_chain(const SpriteChain *chain);

#ifdef __cplusplus
}
#endif

#endif
