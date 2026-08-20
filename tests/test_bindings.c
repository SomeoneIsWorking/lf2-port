#include "bindings.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "test_bindings:%d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

#define CHECK_TEXT(actual, expected) do { \
    const char *check_actual = (actual); \
    if (strcmp(check_actual, (expected)) != 0) { \
        fprintf(stderr, "test_bindings:%d: got '%s', expected '%s'\n", \
                __LINE__, check_actual, (expected)); \
        return 1; \
    } \
} while (0)

int main(int argc, char **argv)
{
    CHECK(argc == 3);
    CHECK(setenv("LF2_CONFIG", argv[2], 1) == 0);

    if (strcmp(argv[1], "read") == 0) {
        config_load();
        CHECK(binding_key_vk(B_ATTACK) == 0x41);
        CHECK(binding_pad_button(B_ATTACK) == SDL_GAMEPAD_BUTTON_NORTH);
        CHECK_TEXT(binding_key_name(binding_key_vk(B_ATTACK)), "A");
        CHECK_TEXT(binding_pad_name(binding_pad_button(B_ATTACK)), "y");
        CHECK(remove(argv[2]) == 0);
        return 0;
    }

    CHECK(strcmp(argv[1], "write") == 0);
    (void)remove(argv[2]);
    CHECK(binding_key_vk(B_ATTACK) == 0x5a);
    CHECK(binding_pad_button(B_ATTACK) == SDL_GAMEPAD_BUTTON_SOUTH);
    CHECK_TEXT(binding_action_id(B_DEFEND), "defend");

    binding_set_key_vk(B_ATTACK, 0x41);
    binding_set_pad_button(B_ATTACK, SDL_GAMEPAD_BUTTON_NORTH);
    CHECK(binding_key_vk(B_ATTACK) == 0x41);
    CHECK_TEXT(binding_key_name(binding_key_vk(B_ATTACK)), "A");
    CHECK(binding_pad_button(B_ATTACK) == SDL_GAMEPAD_BUTTON_NORTH);
    CHECK_TEXT(binding_pad_name(binding_pad_button(B_ATTACK)), "y");
    config_save();
    return 0;
}
