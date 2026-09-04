#ifndef LF2_ENVIRONMENT_H
#define LF2_ENVIRONMENT_H

typedef enum {
#define LF2_ENVIRONMENT_KEY(symbol, name) LF2_ENV_##symbol,
#include "environment_keys.inc"
#undef LF2_ENVIRONMENT_KEY
    LF2_ENV_COUNT
} Lf2EnvironmentKey;

#ifdef __cplusplus
extern "C" {
#endif

const char *lf2_environment_get(Lf2EnvironmentKey key);
int lf2_environment_enabled(Lf2EnvironmentKey key);
const char *lf2_environment_name(Lf2EnvironmentKey key);

#ifdef __cplusplus
}
#endif

#endif /* LF2_ENVIRONMENT_H */
