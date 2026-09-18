/* What LF2 asks a player for, and what it does with the answer, exercised
 * through the shipping policy rather than a copy of it. The real Little
 * Fighter 2 v2.0a executable is never in this repository, so this build is
 * compiled against a fixture identity -- the same seven bytes and CRC32
 * tests/test_game_selection.py uses -- and the shipping validator is otherwise
 * untouched. */
#include "setup_screen_policy.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string &what)
{
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}

void write_file(const std::filesystem::path &path, const std::string &bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

/* The four routes a player may take -- the original installer, a ZIP, an
 * lf2.exe, a tree -- share no file name, and an lf2.exe carried away from its
 * data directory is not an install. Both facts are held by the config, and
 * both break silently if it is turned back into a named, staged set. */
void test_the_screen_asks_for_one_location_it_judges_itself()
{
    const setup_ui::Config config = lf2::setup::screen_config();
    check(config.files.size() == 1, "LF2 shows exactly one requirement");
    check(config.files.front().name.empty(), "the requirement is judged, not matched by file name");
    check(config.placement == setup_ui::Placement::Adopt, "the player's own location is used where it stands");
    check(!config.accepts_archive, "a ZIP is one of the judged routes, not a separate substitute");
    check(config.hint.find("installer") != std::string::npos,
          "the screen names the original installer as a route it accepts");
    check(!config.title.empty() && !config.message.empty(), "the screen is worded for LF2");
}

/* A wrong choice has to come back as a sentence the screen can show. An empty
 * reason would leave the player looking at a screen that refused in silence. */
void test_a_wrong_choice_is_refused_with_a_reason(const std::filesystem::path &root)
{
    std::string executable = "untouched";
    const std::string missing = lf2::setup::judge_selection(root / "no-such-place", executable);
    check(!missing.empty(), "a location that does not exist is refused with a reason");
    check(executable.empty(), "a refusal leaves no resolved executable behind");

    const auto stray = root / "holiday-photo.exe";
    write_file(stray, std::string(4096, '\0'));
    const std::string wrong = lf2::setup::judge_selection(stray, executable);
    check(!wrong.empty(), "a file that is not an LF2 install is refused with a reason");
    check(executable.empty(), "a refused file leaves no resolved executable behind");

    /* An lf2.exe of the right name but the wrong bytes must still be refused:
     * the name is the one thing a wrong file can trivially get right. */
    const auto impostor = root / "tree" / "lf2.exe";
    write_file(impostor, std::string(1024, 'x'));
    write_file(root / "tree" / "data" / "data.txt", "id: 0\n");
    const std::string impostor_reason = lf2::setup::judge_selection(impostor, executable);
    check(!impostor_reason.empty(), "an lf2.exe with the wrong bytes is refused");
    check(impostor_reason.find("v2.0a") != std::string::npos,
          "the refusal says which version the port needs, not just that it said no");
}

/* The session the screen drives is the shipping config and the shipping
 * validator: a refusal has to leave it usable, or the player's only way out of
 * one wrong file is to restart the port. */
void test_a_refused_choice_leaves_the_screen_usable(const std::filesystem::path &root)
{
    const auto first = root / "first.exe";
    const auto second = root / "second.exe";
    write_file(first, std::string(2048, 'a'));
    write_file(second, std::string(2048, 'b'));

    std::string executable;
    setup_ui::Session session(lf2::setup::screen_config(), setup_ui::SessionOptions{},
                              [&executable](const std::vector<setup_ui::StagedFile> &files) {
                                  return files.size() == 1 ? lf2::setup::judge_selection(files.front().path, executable)
                                                           : std::string{"choose one location"};
                              });

    std::string error;
    check(session.add_selected({first}, error) == 1, "the first choice is taken");
    session.validate_if_ready();
    check(session.status() == setup_ui::Status::Rejected, "the first choice is rejected");
    check(!session.message().empty(), "the rejection is on the screen");

    check(session.add_selected({second}, error) == 1, "the player can choose again after a rejection");
    session.validate_if_ready();
    check(session.status() == setup_ui::Status::Rejected, "the second wrong choice is rejected in its turn");
    check(session.entries().size() == 1, "choosing again replaces the choice rather than adding a second row");

    /* Nothing was copied: the port reads the player's own copy where it is. */
    check(session.staging_directory().empty(), "no private copy of the player's files was made");
}

/* The route the player is told to take: a complete install is accepted, and
 * the resolved lf2.exe comes back so the port can start from it. A screen that
 * could only refuse would pass every test above. */
void test_a_complete_install_is_accepted(const std::filesystem::path &root)
{
    const auto tree = root / "LF2";
    write_file(tree / "lf2.exe", "fixture");
    write_file(tree / "data" / "data.txt", "file: data/fixture.dat\n");
    write_file(tree / "data" / "fixture.dat", "asset");

    std::string executable;
    const std::string verdict = lf2::setup::judge_selection(tree / "lf2.exe", executable);
    check(verdict.empty(), "a complete install is accepted: " + verdict);
    check(std::filesystem::path(executable).filename() == "lf2.exe", "the resolved executable comes back");
    check(std::filesystem::exists(executable), "the resolved executable is a path the port can open");

    /* The tree itself, not just the executable inside it, is a route the
     * screen accepts -- it is what an Android SAF selection hands over. */
    std::string from_tree;
    check(lf2::setup::judge_selection(tree, from_tree).empty(), "the install directory is accepted too");
    check(std::filesystem::path(from_tree).filename() == "lf2.exe", "the directory resolves to its lf2.exe");

    /* Accepted through the session the screen actually drives. */
    setup_ui::Session session(lf2::setup::screen_config(), setup_ui::SessionOptions{},
                              [&executable](const std::vector<setup_ui::StagedFile> &files) {
                                  return files.size() == 1 ? lf2::setup::judge_selection(files.front().path, executable)
                                                           : std::string{"choose one location"};
                              });
    std::string error;
    check(session.add_selected({tree / "lf2.exe"}, error) == 1, "the screen takes the choice");
    check(session.status() == setup_ui::Status::Ready, "a single choice completes the requirement");
    session.validate_if_ready();
    check(session.status() == setup_ui::Status::Accepted, "the screen accepts a complete install");
    check(session.message() == "Game files accepted.", "the screen says so in LF2's words");
}

/* Render the shipping screen offscreen at an exact viewport so its wording can
 * be looked at as a player meets it -- LF2's hint line is long, and a phone is
 * where it would wrap or clip. Raw ARGB8888; shared/setup-ui's tools/frame_png.py
 * turns it into a PNG. */
int capture_screen(const std::filesystem::path &destination, int width, int height, float density)
{
    setup_ui::Session session(lf2::setup::screen_config(), setup_ui::SessionOptions{}, nullptr);
    setup_ui::ViewOptions options;
    options.window_title = "Little Fighter 2 setup";
    options.width = width;
    options.height = height;
    options.density_ratio = density;
    options.offscreen = true;
    setup_ui::View view(session, options);
    if (!view.open()) {
        std::cerr << "capture: " << view.last_error() << "\n";
        return 1;
    }
    view.frame();
    std::vector<std::uint32_t> pixels;
    int captured_width = 0;
    int captured_height = 0;
    if (!view.capture(pixels, captured_width, captured_height)) {
        std::cerr << "capture: " << view.last_error() << "\n";
        return 1;
    }
    std::ofstream out(destination, std::ios::binary);
    out.write(reinterpret_cast<const char *>(pixels.data()),
              static_cast<std::streamsize>(pixels.size() * sizeof(std::uint32_t)));
    if (!out) {
        std::cerr << "capture: could not write " << destination << "\n";
        return 1;
    }
    std::cout << captured_width << " " << captured_height << "\n";
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc == 6 && std::string(argv[1]) == "--capture") {
        return capture_screen(argv[2], std::stoi(argv[3]), std::stoi(argv[4]), std::stof(argv[5]));
    }

    /* Under the build tree CTest runs in, not the host's shared temp area. */
    const std::filesystem::path root = std::filesystem::current_path() / "lf2-setup-screen-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    test_the_screen_asks_for_one_location_it_judges_itself();
    test_a_wrong_choice_is_refused_with_a_reason(root / "refusal");
    test_a_refused_choice_leaves_the_screen_usable(root / "reuse");
    test_a_complete_install_is_accepted(root / "accept");

    std::filesystem::remove_all(root);
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "setup screen: all checks passed\n";
    return 0;
}
