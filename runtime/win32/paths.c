/* Windows-path resolution and MSVC text-file translation shared by CRT imports and native
 * overrides. Keeping this out of imports.c makes file identity one cohesive boundary instead
 * of another responsibility inside the legacy import-handler table. */
#include "paths.h"

#include "guest.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static int find_case_insensitive(const char *directory, const char *wanted, char *output, size_t capacity)
{
    DIR *stream = opendir(directory[0] ? directory : ".");
    if (!stream) return 0;
    int found = 0;
    for (struct dirent *entry = readdir(stream); entry; entry = readdir(stream)) {
        if (strcasecmp(entry->d_name, wanted) != 0) continue;
        snprintf(output, capacity, "%s", entry->d_name);
        found = 1;
        break;
    }
    closedir(stream);
    return found;
}

static const char *resolve_host_path(char *path, size_t capacity)
{
    if (access(path, F_OK) == 0) return path;

    char built[1024] = "";
    char work[1024];
    snprintf(work, sizeof work, "%s", path);
    char *save = NULL;
    for (char *token = strtok_r(work, "/", &save); token; token = strtok_r(NULL, "/", &save)) {
        char probe[1024];
        snprintf(probe, sizeof probe, "%s%s", built, token);
        if (access(probe, F_OK) == 0) {
            snprintf(built + strlen(built), sizeof built - strlen(built), "%s/", token);
            continue;
        }
        char directory[1024];
        char match[256];
        snprintf(directory, sizeof directory, "%s", built[0] ? built : ".");
        if (!find_case_insensitive(directory, token, match, sizeof match)) return path;
        snprintf(built + strlen(built), sizeof built - strlen(built), "%s/", match);
    }
    const size_t length = strlen(built);
    if (length && built[length - 1] == '/') built[length - 1] = 0;
    snprintf(path, capacity, "%s", built);
    return path;
}

static const char *translate_path(const char *guest_style)
{
    static char path[1024];
    size_t length = 0;
    for (; guest_style[length] && length + 1 < sizeof path; ++length)
        path[length] = guest_style[length] == '\\' ? '/' : guest_style[length];
    path[length] = 0;
    return resolve_host_path(path, sizeof path);
}

const char *host_path_of(uint32_t guest_string)
{
    return translate_path((const char *)g_mem + guest_string);
}

const char *lf2_host_path(const char *guest_style)
{
    return translate_path(guest_style);
}

char *lf2_read_text(const char *host_path, size_t *length)
{
    FILE *raw = fopen(host_path, "rb");
    if (!raw) return NULL;
    fseek(raw, 0, SEEK_END);
    const long size = ftell(raw);
    rewind(raw);
    if (size < 0) {
        fclose(raw);
        return NULL;
    }
    char *buffer = malloc((size_t)size + 1);
    if (!buffer) {
        fclose(raw);
        return NULL;
    }
    const size_t read = fread(buffer, 1, (size_t)size, raw);
    fclose(raw);

    size_t output = 0;
    for (size_t input = 0; input < read; ++input) {
        if (buffer[input] == '\r' && input + 1 < read && buffer[input + 1] == '\n') continue;
        buffer[output++] = buffer[input];
    }
    buffer[output] = 0;
    *length = output;
    return buffer;
}

FILE *lf2_open_translated(const char *host_path, char **backing)
{
    size_t length = 0;
    char *buffer = lf2_read_text(host_path, &length);
    if (!buffer) return NULL;
    FILE *stream = fmemopen(buffer, length, "r");
    if (!stream) {
        free(buffer);
        return NULL;
    }
    *backing = buffer;
    return stream;
}
