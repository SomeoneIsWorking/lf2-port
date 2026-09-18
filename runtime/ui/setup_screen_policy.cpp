/* setup_screen_policy.cpp -- see setup_screen_policy.h. */
#include "setup_screen_policy.h"

#include "game_data.h"
#include "game_selection.h"

namespace lf2::setup {

setup_ui::Config screen_config()
{
    setup_ui::Config config;
    config.title = "Little Fighter 2 setup";
    config.message = "Point this port at your own copy of Little Fighter 2 v2.0a.";
    config.hint = "the original LF2_v2.0a installer, a ZIP of an installed tree, or lf2.exe inside one";
    config.footer = "Your copy stays where it is; nothing is uploaded.";
    config.browse_label = "Choose your game files";
    config.accepted_message = "Game files accepted.";
    config.files = {setup_ui::FileSpec{"install", "", "Little Fighter 2 v2.0a"}};
    config.placement = setup_ui::Placement::Adopt;
    return config;
}

std::string judge_selection(const std::filesystem::path &selection, std::string &executable)
{
    executable.clear();
    char resolved[GAME_DATA_PATH_CAPACITY];
    char error[GAME_DATA_ERROR_CAPACITY];
#ifdef __ANDROID__
    /* The Activity staged this selection, and an archive inside it is prepared
     * within the same staging wrapper so the tree can be committed atomically. */
    const int ok =
        game_selection_resolve_staged(selection.string().c_str(), resolved, sizeof resolved, error, sizeof error);
#else
    const int ok = game_selection_resolve(selection.string().c_str(), resolved, sizeof resolved, error, sizeof error);
#endif
    if (!ok) {
        return error[0] != 0 ? std::string{error} : std::string{"that location could not be opened"};
    }

    GameData data;
    if (!game_data_validate_executable(resolved, &data)) {
        return data.error[0] != 0 ? std::string{data.error}
                                  : std::string{"that is not a complete Little Fighter 2 v2.0a install"};
    }
    executable = data.executable;
    return {};
}

} // namespace lf2::setup
