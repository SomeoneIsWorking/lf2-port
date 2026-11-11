#ifndef LF2_NETPLAY_ROLLBACK_CHECK_H
#define LF2_NETPLAY_ROLLBACK_CHECK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LF2_ROLLBACK_CHECK: every logic frame runs unshown, is rolled back, and runs again shown; the two
 * runs must end in the same state. A frame where they differ is logged with both hashes. 0 once a
 * quit came first. */
int lf2_rollback_check_frame(uint32_t frame);

#ifdef __cplusplus
}
#endif

#endif
