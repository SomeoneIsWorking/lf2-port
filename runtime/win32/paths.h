#ifndef LF2_PATHS_H
#define LF2_PATHS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Resolve the game's case-insensitive Windows-style paths against the host game tree. */
const char *host_path_of(uint32_t guest_string);
const char *lf2_host_path(const char *guest_style);

/* Read with MSVC text-mode CRLF translation. The caller owns the returned buffer. */
char *lf2_read_text(const char *host_path, size_t *length);

/* The FILE owns no backing buffer; the caller closes it and frees *backing. */
FILE *lf2_open_translated(const char *host_path, char **backing);

#endif
