#include "game_selection.h"

#include <iostream>
#include <string_view>

int main(int argc, char **argv)
{
    const bool staged = argc == 3 && std::string_view(argv[1]) == "--staged";
    if ((!staged && argc != 2) || (staged && argc != 3)) {
        std::cerr << "usage: test_game_selection [--staged] <selection>\n";
        return 2;
    }
    char executable[4096];
    char error[512];
    const char *selection = argv[staged ? 2 : 1];
    const int resolved =
        staged ? game_selection_resolve_staged(selection, executable, sizeof executable, error, sizeof error)
               : game_selection_resolve(selection, executable, sizeof executable, error, sizeof error);
    if (!resolved) {
        std::cerr << error << "\n";
        return 1;
    }
    std::cout << executable << "\n";
    return 0;
}
