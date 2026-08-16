/* See config.h for what this is and why the keys moved here. */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The one keyboard layout's defaults -- the layout that was hardcoded in input.c before this
 * file existed. */
static const uint32_t KEY_DEFAULT[B_N] = {
    0x26, 0x28, 0x25, 0x27, 0x5A, 0x58, 0x43,   /* up down left right Z X C */
};
static const char *const KEY_NAME[B_N] = {
    "key_up", "key_down", "key_left", "key_right",
    "key_attack", "key_jump", "key_defend",
};

/* The file, or NULL when settings are disabled (LF2_CONFIG set-but-empty). */
static const char *config_file(void)
{
    const char *v = getenv("LF2_CONFIG");
    if (v) return *v ? v : NULL;
    return "lf2.cfg";
}

/* The generic store. Fixed size: the port's settings are a handful of short names, and a
 * dynamic map for that is an allocation for nothing. */
enum { SLOT_N = 16 };
static struct { const char *name; char value[32]; } slot[SLOT_N];
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
        slot[i].name = name;
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
    for (int b = 0; b < B_N; b++)
        fprintf(f, "%s %u\n", KEY_NAME[b], (unsigned)config_key_vk(b));
    for (int i = 0; i < slot_n; i++) {
        int is_key = 0;
        for (int b = 0; b < B_N; b++) if (slot[i].name == KEY_NAME[b]) is_key = 1;
        if (!is_key) fprintf(f, "%s %s\n", slot[i].name, slot[i].value);
    }
    fclose(f);
}

uint32_t config_key_vk(int b)
{
    if (b < 0 || b >= B_N) return 0;
    const char *v = config_get(KEY_NAME[b]);
    if (!v || !*v) return KEY_DEFAULT[b];
    char *end = NULL;
    const long n = strtol(v, &end, 0);
    if (end == v || n <= 0 || n >= 256) return KEY_DEFAULT[b];
    return (uint32_t)n;
}

void config_set_key_vk(int b, uint32_t vk)
{
    if (b < 0 || b >= B_N || vk >= 256) return;
    char v[16];
    snprintf(v, sizeof v, "%u", (unsigned)vk);
    config_set(KEY_NAME[b], v);
}

const char *config_key_name(uint32_t vk)
{
    static char buf[16];
    if (vk >= 0x41 && vk <= 0x5A) { snprintf(buf, sizeof buf, "%c", (char)vk); return buf; }
    if (vk >= 0x30 && vk <= 0x39) { snprintf(buf, sizeof buf, "%c", (char)vk); return buf; }
    if (vk >= 0x60 && vk <= 0x69) { snprintf(buf, sizeof buf, "NUM%c", (char)(vk - 0x60 + '0')); return buf; }
    switch (vk) {
    case 0x25: return "LEFT";
    case 0x26: return "UP";
    case 0x27: return "RIGHT";
    case 0x28: return "DOWN";
    case 0x0D: return "ENTER";
    case 0x20: return "SPACE";
    case 0x09: return "TAB";
    case 0x08: return "BKSP";
    case 0x10: return "SHIFT";
    case 0x11: return "CTRL";
    case 0x12: return "ALT";
    case 0x1B: return "ESC";
    default:   snprintf(buf, sizeof buf, "VK %02X", (unsigned)vk); return buf;
    }
}
