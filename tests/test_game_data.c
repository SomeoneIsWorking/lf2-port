#include "game_data.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "test_game_data:%d: %s\n", __LINE__, #condition);                                          \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

static int make_directory(const char *path) { return mkdir(path, 0700) == 0 || errno == EEXIST; }

static const char FIXTURE_EXE[] = "fixture-lf2-v2.0a";

static int write_file(const char *path, const void *data, size_t size)
{
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    const int ok = fwrite(data, 1, size, file) == size;
    return fclose(file) == 0 && ok;
}

static int make_tree(const char *root)
{
    char data_dir[4096], executable[4096], data_file[4096];
    snprintf(data_dir, sizeof data_dir, "%s/data", root);
    snprintf(executable, sizeof executable, "%s/lf2.exe", root);
    snprintf(data_file, sizeof data_file, "%s/data/data.txt", root);
    return make_directory(root) && make_directory(data_dir) &&
           write_file(executable, FIXTURE_EXE, sizeof FIXTURE_EXE - 1) && write_file(data_file, "fixture\n", 8);
}

static void clean_tree(const char *root)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/data/data.txt", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/lf2.exe", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/data", root);
    (void)rmdir(path);
    (void)rmdir(root);
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    char root[4096], executable[4096], appimage[4096], expected[4096];
    snprintf(root, sizeof root, "%s/tree with spaces", argv[1]);
    snprintf(executable, sizeof executable, "%s/lf2.exe", root);
    CHECK(make_directory(argv[1]));
    CHECK(make_tree(root));

    GameData game;
    CHECK(game_data_validate_executable(executable, &game));
    CHECK(strcmp(game.root, root) == 0);

    CHECK(write_file(executable, "fixture-lf2-v2.0b", 17));
    CHECK(!game_data_validate_executable(executable, &game));
    CHECK(strstr(game.error, "is not the supported LF2 v2.0a executable") != NULL);
    CHECK(write_file(executable, FIXTURE_EXE, sizeof FIXTURE_EXE - 1));

    char required_data[4096];
    snprintf(required_data, sizeof required_data, "%s/data/data.txt", root);
    CHECK(remove(required_data) == 0);
    CHECK(!game_data_validate_executable(executable, &game));
    CHECK(strstr(game.error, "data/data.txt is missing") != NULL);
    CHECK(write_file(required_data, "fixture\n", 8));

    char wrong_name[4096];
    snprintf(wrong_name, sizeof wrong_name, "%s/not-lf2.exe", root);
    CHECK(write_file(wrong_name, FIXTURE_EXE, sizeof FIXTURE_EXE - 1));
    CHECK(!game_data_validate_executable(wrong_name, &game));
    CHECK(strstr(game.error, "is not lf2.exe") != NULL);
    CHECK(remove(wrong_name) == 0);

    CHECK(!game_data_discover(wrong_name, root, NULL, argv[1], &game));
    CHECK(strstr(game.error, "not-lf2.exe") != NULL);
    CHECK(game_data_discover(NULL, root, NULL, argv[1], &game));

    char release[4096], sibling[4096];
    snprintf(release, sizeof release, "%s/release", argv[1]);
    snprintf(sibling, sizeof sibling, "%s/game", release);
    snprintf(appimage, sizeof appimage, "%s/LF2 Port.AppImage", release);
    CHECK(make_directory(release));
    CHECK(make_tree(sibling));
    CHECK(game_data_discover(NULL, NULL, appimage, argv[1], &game));
    CHECK(strcmp(game.root, sibling) == 0);

    clean_tree(sibling);
    CHECK(!game_data_discover(NULL, NULL, appimage, argv[1], &game));
    snprintf(expected, sizeof expected, "%s/game", release);
    CHECK(strstr(game.error, expected) != NULL);

    char original[4096];
    CHECK(getcwd(original, sizeof original) != NULL);
    CHECK(game_data_validate_executable(executable, &game));
    CHECK(game_data_activate(&game));
    char active[4096];
    CHECK(getcwd(active, sizeof active) != NULL);
    CHECK(strcmp(active, root) == 0);
    CHECK(chdir(original) == 0);

    clean_tree(root);
    CHECK(rmdir(release) == 0);
    CHECK(rmdir(argv[1]) == 0);
    puts("game_data: discovery, identity, sibling-data, and activation checks passed");
    return 0;
}
