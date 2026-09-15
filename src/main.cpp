#include "game/Content.h"
#include "game/World.h"
#include "game/net/WorldConnection.h"

#include <algorithm>
#include <charconv>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace {
void usage(std::ostream& out) {
    out << "GameServer [--console [content/demo.game]]\n"
        << "GameServer --validate-content [content/demo.game]\n"
        << "GameServer --serve [content/demo.game] [port]\n"
        << "Run from the repository root; default port is 7777.\n";
}
std::uint16_t parsePort(std::string_view text) {
    unsigned int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0 || value > 65535)
        throw std::invalid_argument("port must be a decimal number in 1..65535");
    return static_cast<std::uint16_t>(value);
}
template <typename Character>
std::string asciiArgument(const Character* text) {
    std::string result;
    while (*text) {
        const auto code = static_cast<std::make_unsigned_t<Character>>(*text++);
        if (code > 127) throw std::invalid_argument("mode and port must use ASCII characters");
        result += static_cast<char>(code);
    }
    return result;
}
} // namespace

template <typename Character>
int applicationMain(int argc, Character* argv[]) {
    try {
        const std::string mode = argc > 1 ? asciiArgument(argv[1]) : "--console";
        if (mode == "--help" && argc == 2) { usage(std::cout); return 0; }
        if ((mode != "--console" && mode != "--validate-content" && mode != "--serve") ||
            argc > (mode == "--serve" ? 4 : 3)) {
            usage(std::cerr);
            return 2;
        }
        // Preserve native path encoding: wchar_t on Windows, UTF-8 bytes on Unix.
        const std::filesystem::path path = argc > 2 ? std::filesystem::path(argv[2])
            : std::filesystem::path("content/demo.game");
        const auto port = argc > 3 ? parsePort(asciiArgument(argv[3])) : std::uint16_t{7777};
        game::World world(game::Content::load(path));
        if (mode == "--validate-content") {
            const auto& content = world.content();
            std::cout << "Valid content: maps=" << content.maps.size() << " characters=" << content.characters.size()
                << " skills=" << content.skills.size() << " effects=" << content.effects.size() << '\n';
            return 0;
        }
        const auto map = world.content().maps.find("arena");
        if (map == world.content().maps.end()) throw std::runtime_error("development host requires map arena");
        if (world.content().characters.count("mob")) {
            const auto id = world.spawn("mob", map->first, 2,
                {std::min(140.0, map->second.width), std::min(100.0, map->second.height)}, "TrainingDummy");
            std::cout << "Training dummy id=" << id << '\n';
        }
        world.takeEvents(); // Bootstrap events precede all sessions.
        if (mode == "--serve") return game::net::runServer(world, port);
        return game::net::runConsole(world, std::cin, std::cout);
    } catch (const std::exception& error) {
        std::cerr << "GameServer: " << error.what() << '\n';
        return 1;
    }
}

#if defined(_WIN32)
int wmain(int argc, wchar_t* argv[]) { return applicationMain(argc, argv); }
#else
int main(int argc, char* argv[]) { return applicationMain(argc, argv); }
#endif
 