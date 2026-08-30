/* See options.h for what this is and why the env vars are initial values rather than the
 * mechanism. */
#include "options.h"

#include <stdlib.h>
#include <string.h>

#include "config.h"

/* -1 until first read, so the env pin is consulted exactly once and the menu owns the value
 * from then on. */
static int renderer = -1, lighting = -1, touch_controls = -1;

static int pinned_off(const char *v)
{
    return v && (strcmp(v, "off") == 0 || strcmp(v, "0") == 0);
}

int opt_renderer_engine(void)
{
    if (renderer < 0) {
        const char *v = getenv("LF2_ENGINE");
        if (v && *v) {
            /* The env pin is the test arm, and it wins outright (tools/routes pin it). */
            renderer = strcmp(v, "0") != 0;
        } else {
            const char *c = config_get("renderer");
            if (c) renderer = strcmp(c, "classic") != 0;
            else
                renderer = 1; /* the default moved to the engine when the effects became
                               * engine-only (issue #69): a classic default would have
                               * been a default with no shading. */
        }
    }
    return renderer;
}

void opt_set_renderer_engine(int on)
{
    renderer = on != 0;
}

int opt_lighting(void)
{
    if (lighting < 0) {
        const char *v = getenv("LF2_HD2D");
        if (v && *v) lighting = pinned_off(v) ? 0 : 1;
        else {
            const char *c = config_get("lighting");
            lighting = c ? strcmp(c, "off") != 0 : 1;
        }
    }
    return lighting;
}

void opt_set_lighting(int on)
{
    lighting = on != 0;
}

int opt_touch_controls(void)
{
    if (touch_controls < 0) {
        const char *configured = config_get("touch_controls");
        touch_controls = !configured || strcmp(configured, "off") != 0;
    }
    return touch_controls;
}

void opt_set_touch_controls(int on)
{
    touch_controls = on != 0;
}

/* The light intensity is a plain float once its pin has been resolved, so it lives in one
 * static rather than the -1-means-unread pattern of the ints above. */
static float value_light_intensity = -1.0f;

float opt_light_intensity(void)
{
    if (value_light_intensity < 0.0f) {
        const char *v = getenv("LF2_HD2D_KEY");
        const char *c = v && *v ? v : config_get("light_intensity");
        value_light_intensity = c && *c ? (float)atof(c) : 1.48f;
        if (value_light_intensity <= 0.0f || value_light_intensity > 8.0f) value_light_intensity = 1.48f;
    }
    return value_light_intensity;
}

void opt_set_light_intensity(float v)
{
    if (v < 0.05f) v = 0.05f;
    if (v > 8.0f) v = 8.0f;
    value_light_intensity = v;
}

/* The sampling chain follows the same shape as the renderer's pins: the environment wins once,
 * then the config file, then the default -- which is the empty chain, the original picture. */
static SpriteChain chain;
static int chain_read;

const SpriteChain *opt_sprite_chain(void)
{
    if (!chain_read) {
        chain_read = 1;
        spritechain_clear(&chain);
        const char *v = getenv("LF2_SPRITE_PASSES");
        const char *spec = v && *v ? v : config_get("sprite_passes");
        char err[128];
        if (spec && !spritechain_parse(spec, &chain, err, sizeof err)) {
            fprintf(stderr, "sprite passes: %s in \"%s\"; no sampling chain is in effect\n", err, spec);
            spritechain_clear(&chain);
        }
    }
    return &chain;
}

void opt_set_sprite_chain(const SpriteChain *c)
{
    chain_read = 1;
    chain = *c;
}
