#include "environment.h"

#include <stddef.h>
#include <stdlib.h>

static const char *const ENVIRONMENT_NAMES[] = {
#define LF2_ENVIRONMENT_KEY(symbol, name) [LF2_ENV_##symbol] = name,
#include "environment_keys.inc"
#undef LF2_ENVIRONMENT_KEY
};

const char *lf2_environment_name(Lf2EnvironmentKey key)
{
    return key >= 0 && key < LF2_ENV_COUNT ? ENVIRONMENT_NAMES[key] : NULL;
}

const char *lf2_environment_get(Lf2EnvironmentKey key)
{
    const char *name = lf2_environment_name(key);
    return name ? getenv(name) : NULL;
}

int lf2_environment_enabled(Lf2EnvironmentKey key)
{
    return lf2_environment_get(key) != NULL;
}
