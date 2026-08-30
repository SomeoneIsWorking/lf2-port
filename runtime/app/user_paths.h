#ifndef LF2_USER_PATHS_H
#define LF2_USER_PATHS_H

#include <stddef.h>

/* Compose the port-owned settings filename below Lucent's platform user-data directory. */
int user_paths_config_file(char *output, size_t capacity, int create_directory, char *error, size_t error_capacity);

#endif
