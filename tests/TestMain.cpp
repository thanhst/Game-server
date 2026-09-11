#include "game/Content.h"
#include <exception>
#include <iostream>

void runContentTests();
void runWorldTests();
void runProtocolTests();
void runLegacySkillTests();

int main(int argc, char** argv)
{
    try {
        runContentTests();
        runWorldTests();
        runProtocolTests();
        runLegacySkillTests();
        if (argc > 1) game::Content::load(argv[1]);
        std::cout << "Content, world, and protocol checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
