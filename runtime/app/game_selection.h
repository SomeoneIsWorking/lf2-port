#ifndef LF2_GAME_SELECTION_H
#define LF2_GAME_SELECTION_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resolve a setup selection to lf2.exe. A direct executable is returned unchanged, a
 * selected directory resolves its direct child, and a ZIP or original LF2 installer is
 * safely extracted below the OS user-data directory before returning its lf2.exe. */
int game_selection_resolve(const char *selection, char *executable, size_t executable_capacity, char *error,
                           size_t error_capacity);

/* Android passes Activity-owned private staging selections here. Archives are prepared and
 * validated inside the same staging wrapper so the Activity can atomically commit that tree. */
int game_selection_resolve_staged(const char *selection, char *executable, size_t executable_capacity, char *error,
                                  size_t error_capacity);

#ifdef __cplusplus
}
#endif

#endif
