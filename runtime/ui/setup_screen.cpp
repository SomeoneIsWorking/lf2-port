/* setup_screen.cpp -- see setup_screen.h.
 *
 * The screen itself is the shared setup-ui module, the same one Benefactor
 * shows; this owns the order LF2's picker, resolver, and validator run in, and
 * keeps a rejection on the screen instead of tearing it down. What LF2 asks
 * for and how it judges the answer is setup_screen_policy. */
#include "setup_screen.h"

#include "game_picker.h"
#include "lf2_log.h"
#include "setup_screen_policy.h"

#include "setup_ui/setup_ui.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

SetupScreenResult setup_screen_run(const char *message, char *executable, size_t executable_capacity, char *error,
                                   size_t error_capacity)
{
    if (executable == nullptr || executable_capacity == 0 || error == nullptr || error_capacity == 0) {
        return SETUP_SCREEN_ERROR;
    }
    executable[0] = 0;
    error[0] = 0;

    const auto report = [&](const std::string &text) { std::snprintf(error, error_capacity, "%s", text.c_str()); };

    setup_ui::Config config = lf2::setup::screen_config();
    if (message != nullptr && message[0] != 0) {
        config.message = message;
    }

    std::string resolved;
    /* Nothing is staged under Placement::Adopt, so the session never touches a
     * staging root and none is supplied. */
    setup_ui::Session session(config, setup_ui::SessionOptions{},
                              [&resolved](const std::vector<setup_ui::StagedFile> &files) {
                                  resolved.clear();
                                  if (files.size() != 1) {
                                      return std::string{"choose one installer, archive, executable, or folder"};
                                  }
                                  return lf2::setup::judge_selection(files.front().path, resolved);
                              });

    setup_ui::ViewOptions options;
    options.window_title = "Little Fighter 2 setup";
    setup_ui::View view(session, options);
    if (!view.open()) {
        report(view.last_error());
        lf2_log_writef(LF2_LOG_INFO, "setup_screen", "setup: the setup screen could not open: %s\n",
                       view.last_error().c_str());
        return SETUP_SCREEN_ERROR;
    }

    bool cancelled = false;
    while (view.running()) {
        for (const setup_ui::Request &request : view.poll()) {
            switch (request.kind) {
            case setup_ui::RequestKind::Browse: {
                char selection[GAME_PICKER_PATH_CAPACITY];
                const GamePickResult picked = game_picker_choose(config.message.c_str(), selection, sizeof selection);
                if (picked == GAME_PICK_ERROR) {
                    report("the file picker is unavailable on this system");
                    view.close();
                    return SETUP_SCREEN_ERROR;
                }
                if (picked == GAME_PICK_SELECTED) {
                    std::string selection_error;
                    session.add_selected({std::filesystem::path{selection}}, selection_error);
                    if (!selection_error.empty()) {
                        lf2_log_writef(LF2_LOG_INFO, "setup_screen", "setup: %s\n", selection_error.c_str());
                    }
                }
                break;
            }
            case setup_ui::RequestKind::Start:
                session.validate_if_ready();
                if (session.status() == setup_ui::Status::Accepted) {
                    view.finish();
                }
                break;
            case setup_ui::RequestKind::Cancel:
                cancelled = true;
                view.finish();
                break;
            }
        }

        /* Judging a selection the moment it arrives is what makes a rejection a
         * line on the screen instead of a dialog the player has to dismiss. */
        session.validate_if_ready();
        if (session.status() == setup_ui::Status::Accepted) {
            /* One more frame so the player sees the set accepted before the
             * screen gives way to the game. */
            view.frame();
            view.close();
            const int written = std::snprintf(executable, executable_capacity, "%s", resolved.c_str());
            if (written < 0 || static_cast<size_t>(written) >= executable_capacity) {
                executable[0] = 0;
                report("the resolved game path is too long for this port to hold");
                return SETUP_SCREEN_ERROR;
            }
            return SETUP_SCREEN_RESOLVED;
        }
        view.frame();
    }

    view.close();
    if (cancelled) {
        report("setup was dismissed before a Little Fighter 2 install was chosen");
        return SETUP_SCREEN_CANCELLED;
    }
    report(session.message().empty() ? "no Little Fighter 2 install was chosen" : session.message());
    return SETUP_SCREEN_CANCELLED;
}
