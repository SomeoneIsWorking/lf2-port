#ifndef LF2_UPDATE_H
#define LF2_UPDATE_H

/* Whether this package has a platform updater, and request its user-facing flow. */
int update_supported(void);
void update_request(void);

#endif
