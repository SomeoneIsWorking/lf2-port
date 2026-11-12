#ifndef LF2_PATHS_H
#define LF2_PATHS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Resolve the game's case-insensitive Windows-style paths against the host game tree. */
const char *host_path_of(uint32_t guest_string);
const char *lf2_host_path(const char *guest_style);
/* The game's data\temporary.txt resolves to a file of this process's own in `directory`; files
 * there left by processes that are gone are deleted. */
int lf2_paths_set_scratch_directory(const char *directory);
void lf2_paths_remove_scratch(void);

/* Read with MSVC text-mode CRLF translation. The caller owns the returned buffer. */
char *lf2_read_text(const char *host_path, size_t *length);

/* The FILE owns no backing buffer; the caller closes it and frees *backing. */
FILE *lf2_open_translated(const char *host_path, char **backing);

#endif
