#include <emscripten/emscripten.h>

#include <lucent/platform_c.h>
#include <lucent/web.h>

extern "C" int lf2_native_main(int argc, char **argv);

static void report_setup(const char *message, int failed)
{
    MAIN_THREAD_EM_ASM({
        if (Module['onSetupStatus']) Module['onSetupStatus'](UTF8ToString($0), Boolean($1));
    }, message, failed);
}

int main(int argc, char **argv)
{
    if (!lucent_web_mount_storage("/opfs")) {
        report_setup("The browser could not open private game storage.", 1);
        return 1;
    }
    int result = 1;
    if (!lucent_platform_set_user_data_directory("/opfs/user")) {
        report_setup("The browser could not select private game storage.", 1);
    } else {
        report_setup(argc > 1 ? "Checking and unpacking your LF2 game files..."
                              : "Checking your saved LF2 installation...", 0);
        result = lf2_native_main(argc, argv);
        if (result == 0) {
            MAIN_THREAD_EM_ASM({
                if (Module['onGameReady']) Module['onGameReady']();
            });
        }
    }
    if (!lucent_web_unmount_storage("/opfs")) {
        report_setup("Private game storage could not be closed safely.", 1);
        result = 1;
    }
    return result;
}
