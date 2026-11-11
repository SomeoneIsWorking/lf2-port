#ifndef LF2_USER_PATHS_H
#define LF2_USER_PATHS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compose the port-owned settings filename below Lucent's platform user-data directory. */
int user_paths_config_file(char *output, size_t capacity, int create_directory, char *error, size_t error_capacity);
/* Where the embedded ZeroTier node keeps its identity, so a player's code stays the same. */
int user_paths_zerotier_directory(char *output, size_t capacity, char *error, size_t error_capacity);

#ifdef __cplusplus
}
#endif

#endif
