#include "game_selection.h"

#include <iostream>

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "usage: test_game_selection <selection>\n";
        return 2;
    }
    char executable[4096];
    char error[512];
    if (!game_selection_resolve(argv[1], executable, sizeof executable, error, sizeof error)) {
        std::cerr << error << "\n";
        return 1;
    }
    std::cout << executable << "\n";
    return 0;
}
