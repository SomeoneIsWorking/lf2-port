/* The original match function keys, exposed as typed port commands for RmlUi. */
#ifndef LF2_CHEATS_H
#define LF2_CHEATS_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    CHEAT_SECRET_FIGHTERS,
    CHEAT_UNLIMITED_MP,
    CHEAT_RESTORE,
    CHEAT_DROP_ITEMS,
    CHEAT_DESTROY_ITEMS,
    CHEAT_ACTION_COUNT
} CheatAction;

typedef struct {
    CheatAction action;
    const char *id;
    const char *key;
    const char *label;
    const char *detail;
    uint32_t vk;
    int match_only;
} CheatDescriptor;

const CheatDescriptor *cheats_descriptors(size_t *count);
int cheats_action_from_id(const char *id, CheatAction *action);

/* Activate a cheat. The caller must close any modal document first; the ordinary input
 * boundary deliberately blocks keys while RmlUi is up. */
int cheats_request(CheatAction action);
void cheats_tick(void);
void cheats_report(void);

#endif
