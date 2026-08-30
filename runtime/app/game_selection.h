#ifndef LF2_GAME_SELECTION_H
#define LF2_GAME_SELECTION_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resolve a setup selection to lf2.exe. A direct executable is returned unchanged, a
 * selected directory resolves its direct child, and a ZIP is extracted through Lucent
 * below the OS user-data directory before returning its single nested lf2.exe. */
int game_selection_resolve(const char *selection, char *executable, size_t executable_capacity, char *error,
                           size_t error_capacity);

#ifdef __cplusplus
}
#endif

#endif
