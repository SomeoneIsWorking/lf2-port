/* See config.h for what this is and why the keys moved here. */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The file, or NULL when settings are disabled (LF2_CONFIG set-but-empty). */
static const char *config_file(void)
{
    const char *v = getenv("LF2_CONFIG");
    if (v) return *v ? v : NULL;
    return "lf2.cfg";
}

/* The generic store. Fixed size: the port's settings are a handful of short names, and a
 * dynamic map for that is an allocation for nothing. */
enum { SLOT_N = 32 };
static struct { char name[32]; char value[32]; } slot[SLOT_N];
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

void config_set(const char *name, const char *value)
{
    int i = find_slot(name);
    if (i < 0) {
        if (slot_n >= SLOT_N) return;
        i = slot_n++;
        snprintf(slot[i].name, sizeof slot[i].name, "%s", name);
    }
    snprintf(slot[i].value, sizeof slot[i].value, "%s", value ? value : "");
}

void config_load(void)
{
    const char *path = config_file();
    FILE *f = path ? fopen(path, "r") : NULL;
    if (!f) return;                              /* no file is a valid, default state */
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char name[32], value[32];
        const int n = sscanf(line, "%31s %31s", name, value);
        if (n == 2 && name[0] != '#') config_set(name, value);
    }
    fclose(f);
}

void config_save(void)
{
    const char *path = config_file();
    FILE *f = path ? fopen(path, "w") : NULL;
    if (!f) return;
    fprintf(f, "# LF2 port settings\n");
    for (int i = 0; i < slot_n; i++) fprintf(f, "%s %s\n", slot[i].name, slot[i].value);
    fclose(f);
}
