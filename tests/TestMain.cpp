#include "game/Content.h"
#include <exception>
#include <iostream>

void runContentTests();
void runResourceTests();
void runObjectTests();
void runMapTests();
void runCameraTests();
void runWorldTests();
void runProtocolTests();

int main(int argc, char** argv)
{
    try {
        runContentTests();
        runResourceTests();
        runObjectTests();
        runMapTests();
        runCameraTests();
        runWorldTests();
        runProtocolTests();
        if (argc > 1) game::Content::load(argv[1]);
        std::cout << "Content, resource, object, map, camera, world, and protocol checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
