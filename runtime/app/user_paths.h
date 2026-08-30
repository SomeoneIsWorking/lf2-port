#ifndef LF2_USER_PATHS_H
#define LF2_USER_PATHS_H

#include <stddef.h>

/* Resolve the port-owned settings file. Linux follows XDG_CONFIG_HOME; the other
 * packaged platforms use SDL's application-specific writable directory. */
int user_paths_config_file(char *output, size_t capacity, int create_directory, char *error, size_t error_capacity);

#endif
