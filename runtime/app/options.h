/* The port's renderer options -- which renderer draws, and which effects it runs.
 *
 * WHY THIS EXISTS, and why it is not three getenv calls. The renderer and its effects were
 * chosen by LF2_ENGINE / LF2_HD2D / LF2_DOF and nowhere else: a setting a player cannot find
 * is not a feature, which is the project's standing rule for the whole LF2_* namespace
 * (issue #69). The state lives here, the pause menu's Options page owns it while the game is
 * up, and the consumers -- engine.c, hd2d.c -- READ it rather than caching it, so a change
 * from the menu applies to the next frame.
 *
 * THE ENV VARS ARE STILL HONOURED, ONCE, AS THE INITIAL VALUE. The route scripts under
 * tools/routes pin them to keep every A/B arm meaning exactly what it meant, and a route never
 * opens the pause menu, so an initial value IS the whole run's value there. That is what lets the player's switch and
 * the test's control arm be the same variable without either one lying about the other.
 *
 * Nothing here persists across runs, and that is deliberate rather than missing: the light's
 * two angles do not persist either, and the Options page is the place that decides otherwise
 * for all of them at once.
 */
#ifndef LF2_OPTIONS_H
#define LF2_OPTIONS_H

/* Which renderer draws: the port's own engine (SDL_GPU, with the effects) or the classic
 * SDL_Render path, which stays as the plain-picture fallback and the byte-identity control
 * arm. Default ENGINE. LF2_ENGINE=0 pins classic at startup, any other non-empty value pins
 * the engine. */
int  opt_renderer_engine(void);
void opt_set_renderer_engine(int on);

/* The HD2D lighting and its cast shadows. Default ON. LF2_HD2D=off pins it off at startup. */
int  opt_lighting(void);
void opt_set_lighting(int on);

/* The depth of field. Default ON. LF2_DOF=off pins it off at startup. */
int  opt_dof(void);
void opt_set_dof(int on);

#endif
