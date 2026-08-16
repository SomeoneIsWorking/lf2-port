/* The port's own settings file, and the device mapping it exists for.
 *
 * WHY THIS EXISTS, and what it is not. The keyboard was one hardcoded layout in
 * runtime/overrides/input.c -- arrows to move, Z attack, X jump, C defend -- with no way to
 * change it and no reason to keep the game's own data/control.txt layouts (they are per-player
 * and only editable from the front end, which the port is removing -- issues #70, #71). A
 * mapping a player cannot change is not a mapping, so the seven keys move here, editable from
 * the pause menu's CONTROLS page, and persist across runs.
 *
 * THE FILE is `lf2.cfg` beside the game tree (the port runs with the game tree as its cwd),
 * overridable with LF2_CONFIG. An EMPTY LF2_CONFIG disables the file entirely, which is how the
 * scripted routes stay deterministic: tools/e2e.sh exports it, so a developer's personal key
 * bindings can never re-aim a route's scripted key presses.
 *
 * The format is one `name value` per line, `#` comments. It is deliberately the seed for the
 * rest of the port's options rather than a key-only bag: runtime/app/options.{c,h} says the
 * renderer/lighting/DOF and the light angles deliberately do not persist, and this file is the
 * one place that will decide otherwise for all of them at once when they do.
 */
#ifndef LF2_CONFIG_H
#define LF2_CONFIG_H

#include <stdint.h>

/* The seven buttons, in the GAME's own order -- up, down, left, right, attack, jump, defend
 * -- which is the order runtime/overrides/input.c reads and writes them (BTN_BIT there). */
enum { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_ATTACK, B_JUMP, B_DEFEND, B_N };

/* Read the file (or do nothing when there is none). Safe to call once at startup. */
void config_load(void);

/* Write the current settings to the file (or do nothing when there is none). */
void config_save(void);

/* The bound virtual key for button `b` (0..B_N-1), with the default applied when unset. */
uint32_t config_key_vk(int b);
void     config_set_key_vk(int b, uint32_t vk);   /* set in memory; caller then saves */

/* A virtual key's name, for the CONTROLS page. Returns a static buffer. */
const char *config_key_name(uint32_t vk);

/* The generic store the rest of the options will persist through (renderer, lighting, DOF,
 * the light angles). Returns NULL when the name is unset. */
const char *config_get(const char *name);
void        config_set(const char *name, const char *value);

#endif
