#ifndef LF2_PORT_RESOURCES_H
#define LF2_PORT_RESOURCES_H

#include <stddef.h>

/* Resolve the packaged, port-owned stages directory for this platform. The caller may
 * still try a repository-relative `stages` directory as its development fallback. */
int port_resources_stages(char *output, size_t capacity);

#endif
