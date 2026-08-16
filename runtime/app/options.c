/* See options.h for what this is and why the env vars are initial values rather than the
 * mechanism. */
#include "options.h"

#include <stdlib.h>
#include <string.h>

/* -1 until first read, so the env pin is consulted exactly once and the menu owns the value
 * from then on. */
static int renderer = -1, lighting = -1, dof = -1;

static int pinned_off(const char *v)
{
    return v && (strcmp(v, "off") == 0 || strcmp(v, "0") == 0);
}

int opt_renderer_engine(void)
{
    if (renderer < 0) {
        const char *v = getenv("LF2_ENGINE");
        /* The default moved to the engine when the effects became engine-only (issue #69):
         * a classic default would have been a default with no shading. =0 keeps the classic
         * path one word away, as the A/B control arm. */
        renderer = (v && *v && strcmp(v, "0") != 0) || !v ? 1 : 0;
    }
    return renderer;
}

void opt_set_renderer_engine(int on) { renderer = on != 0; }

int opt_lighting(void)
{
    if (lighting < 0) lighting = pinned_off(getenv("LF2_HD2D")) ? 0 : 1;
    return lighting;
}

void opt_set_lighting(int on) { lighting = on != 0; }

int opt_dof(void)
{
    if (dof < 0) dof = pinned_off(getenv("LF2_DOF")) ? 0 : 1;
    return dof;
}

void opt_set_dof(int on) { dof = on != 0; }
