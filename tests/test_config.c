#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "test_config:%d: %s\n", __LINE__, #condition);                                             \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

int main(int argc, char **argv)
{
    CHECK(argc == 3);
    CHECK(unsetenv("LF2_CONFIG") == 0);
    CHECK(setenv("XDG_CONFIG_HOME", argv[2], 1) == 0);
    if (strcmp(argv[1], "write") == 0) {
        CHECK(config_set("game_dir", "/games/Little Fighter 2 v2.0a"));
        CHECK(config_save());
        return 0;
    }

    CHECK(strcmp(argv[1], "read") == 0);
    config_load();
    CHECK(strcmp(config_get("game_dir"), "/games/Little Fighter 2 v2.0a") == 0);
    char file[4096], directory[4096];
    snprintf(directory, sizeof directory, "%s/lf2-port", argv[2]);
    snprintf(file, sizeof file, "%s/lf2.cfg", directory);
    CHECK(remove(file) == 0);
    CHECK(rmdir(directory) == 0);
    puts("config: XDG path and space-preserving values passed");
    return 0;
}
