#ifndef LF2_SETUP_SCREEN_POLICY_H
#define LF2_SETUP_SCREEN_POLICY_H

#include "setup_ui/setup_ui.h"

#include <filesystem>
#include <string>

/* What LF2 asks a player for, and how it judges the answer. Separated from the
 * screen's loop so both can be exercised without opening a window: the loop is
 * SDL and RmlUi, this is the port's own policy. */
namespace lf2::setup {

/* The requirement LF2 shows: one location, judged by judge_selection below.
 * The four things a player may point at -- the original v2.0a installer, a ZIP
 * of an installed tree, an lf2.exe inside one, or the tree itself -- share no
 * file name, so this is deliberately a single unnamed FileSpec, and the
 * player's own location is adopted rather than copied (an lf2.exe carried away
 * from its data directory is not an install). */
setup_ui::Config screen_config();

/* Resolve one chosen location and decide whether it really is Little Fighter 2
 * v2.0a. Returns an empty string when it is, leaving `executable` set to the
 * resolved lf2.exe; otherwise returns the reason to show the player. */
std::string judge_selection(const std::filesystem::path &selection, std::string &executable);

} // namespace lf2::setup

#endif
