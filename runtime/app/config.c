/* See config.h for what this is and why the keys moved here. */
#include "lf2_log.h"
#include "environment.h"
#include "config.h"

#include "user_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The file, or NULL when settings are disabled (LF2_CONFIG set-but-empty). */
static const char *config_file(int create_directory)
{
    const char *v = lf2_environment_get(LF2_ENV_CONFIG);
    if (v) return *v ? v : NULL;
    static char path[4096];
    char error[512];
    if (!user_paths_config_file(path, sizeof path, create_directory, error, sizeof error)) {
        lf2_log_writef(LF2_LOG_INFO, "config", "config: %s\n", error);
        return NULL;
    }
    return path;
}

/* The generic store. Fixed size: the port's settings are a handful of short names, and a
 * dynamic map for that is an allocation for nothing. */
enum { SLOT_N = 32 };
static struct {
    char name[32];
    char value[4096];
} slot[SLOT_N];
static int slot_n;

static int find_slot(const char *name)
{
    for (int i = 0; i < slot_n; i++)
        if (strcmp(slot[i].name, name) == 0) return i;
    return -1;
}

const char *config_get(const char *name)
{
    const int i = find_slot(name);
    return i < 0 ? NULL : slot[i].value;
}

int config_set(const char *name, const char *value)
{
    const char *stored_value = value ? value : "";
    if (strlen(name) >= sizeof slot[0].name || strlen(stored_value) >= sizeof slot[0].value) return 0;
    int i = find_slot(name);
    if (i < 0) {
        if (slot_n >= SLOT_N) return 0;
        i = slot_n++;
        snprintf(slot[i].name, sizeof slot[i].name, "%s", name);
    }
    snprintf(slot[i].value, sizeof slot[i].value, "%s", stored_value);
    return 1;
}

void config_load(void)
{
    const char *path = config_file(0);
    FILE *f = path ? fopen(path, "r") : NULL;
    if (!f) return; /* no file is a valid, default state */
    char line[8192];
    while (fgets(line, sizeof line, f)) {
        char name[32];
        int consumed = 0;
        if (sscanf(line, "%31s%n", name, &consumed) != 1 || name[0] == '#') continue;
        char *value = line + consumed;
        while (*value == ' ' || *value == '\t') ++value;
        value[strcspn(value, "\r\n")] = 0;
        if (*value) (void)config_set(name, value);
    }
    fclose(f);
}

int config_save(void)
{
    const char *path = config_file(1);
    if (!path) {
        const char *override = lf2_environment_get(LF2_ENV_CONFIG);
        return override && !*override;
    }
    FILE *f = path ? fopen(path, "w") : NULL;
    if (!f) {
        lf2_log_perror("config", path);
        return 0;
    }
    fprintf(f, "# LF2 port settings\n");
    for (int i = 0; i < slot_n; i++) fprintf(f, "%s %s\n", slot[i].name, slot[i].value);
    if (fclose(f) != 0) {
        lf2_log_perror("config", path);
        return 0;
    }
    return 1;
}
