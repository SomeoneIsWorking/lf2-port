/* The port's own settings file, and the device mapping it exists for.
 *
 * WHY THIS EXISTS, and what it is not. The keyboard was one hardcoded layout in
 * runtime/overrides/input.c -- arrows to move, Z attack, X jump, C defend -- with no way to
 * change it and no reason to keep the game's own data/control.txt layouts (they are per-player
 * and only editable from the front end, which the port is removing -- issues #70, #71). A
 * mapping a player cannot change is not a mapping, so the seven keys move here, editable from
 * the RmlUi settings screen, and persist across runs.
 *
 * THE FILE is `lf2.cfg` in the OS-owned per-user application configuration directory,
 * overridable with LF2_CONFIG. An EMPTY LF2_CONFIG disables the file entirely, which is how the
 * scripted routes stay deterministic: tools/e2e.py exports it, so a developer's personal key
 * bindings can never re-aim a route's scripted key presses. It never defaults to the checkout,
 * current working directory, game tree, or an AppImage mount.
 *
 * The format is one `name value` per line, `#` comments. It is deliberately the seed for the
 * rest of the port's options rather than a key-only bag: renderer and character-lighting
 * choices are stored through the same owner as the action bindings.
 */
#ifndef LF2_CONFIG_H
#define LF2_CONFIG_H

/* Read the file (or do nothing when there is none). Safe to call once at startup. */
void config_load(void);

/* Write the current settings to the file (or do nothing when there is none). */
int config_save(void);

/* The generic store used by cohesive option/binding owners. Returns NULL when unset. */
const char *config_get(const char *name);
int config_set(const char *name, const char *value);

#endif
