#include "game/net/DebugProtocol.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace game::net {
namespace {
std::string clean(std::string_view text) {
    std::string result;
    for (unsigned char ch : text) {
        if (result.size() == 256) break;
        result += (ch >= 32 && ch <= 126) ? static_cast<char>(ch) : '?';
    }
    return result;
}
ProtocolReply error(const std::string& reason) {
    return {"GAME/1 ERROR " + clean(reason), false};
}
bool identifier(std::string_view text, std::size_t maxLength = 64) {
    if (text.empty() || text.size() > maxLength) return false;
    return std::all_of(text.begin(), text.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.';
    });
}
bool number(std::string_view text, std::uint64_t& value) {
    if (text.empty()) return false;
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
bool real(std::string_view text, double& value) {
    std::istringstream input{std::string(text)};
    input.imbue(std::locale::classic());
    input >> std::noskipws >> value;
    return input && input.peek() == std::char_traits<char>::eof() && std::isfinite(value);
}
std::vector<std::string_view> tokens(std::string_view message) {
    std::vector<std::string_view> result;
    while (!message.empty()) {
        const auto start = message.find_first_not_of(' ');
        if (start == std::string_view::npos) break;
        message.remove_prefix(start);
        const auto end = message.find(' ');
        result.push_back(message.substr(0, end));
        if (result.size() > 5) break;
        if (end == std::string_view::npos) break;
        message.remove_prefix(end + 1);
    }
    return result;
}
std::string entityLine(const World& world, const Entity& entity) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(10) << "ENTITY id=" << entity.id
        << " archetype=" << clean(entity.archetype) << " name=" << clean(entity.name)
        << " map=" << clean(entity.map) << " team=" << entity.team
        << " x=" << entity.position.x << " y=" << entity.position.y
        << " hp=" << entity.hp << " max_hp=" << world.attribute(entity, "hp")
        << " mana=" << entity.mana << " max_mana=" << world.attribute(entity, "mana")
        << " alive=" << (entity.alive() ? 1 : 0);
    return out.str();
}
} // namespace

DebugProtocol::DebugProtocol(World& world, bool console) : world_(world), console_(console) {
    for (const auto& pair : world_.entities()) knownMaps_[pair.first] = pair.second.map;
}
ProtocolReply DebugProtocol::connect(std::uint64_t session) {
    if (!sessions_.emplace(session, 0).second) return error("session_already_open");
    return {"GAME/1 HELLO development=1 authentication=none commands=PING,CREATE,MOVE,CAST,STATE,ENTITIES,QUIT", false};
}
EntityId DebugProtocol::entityFor(std::uint64_t session) const {
    const auto found = sessions_.find(session);
    return found == sessions_.end() ? 0 : found->second;
}
bool DebugProtocol::canSee(std::uint64_t session, const std::string& map) const {
    const Entity* entity = world_.find(entityFor(session));
    return entity && entity->map == map;
}
void DebugProtocol::disconnect(std::uint64_t session) {
    const auto found = sessions_.find(session);
    if (found == sessions_.end()) return;
    if (const Entity* entity = world_.find(found->second)) knownMaps_[entity->id] = entity->map;
    world_.despawn(found->second);
    sessions_.erase(found);
}
ProtocolReply DebugProtocol::dispatch(std::uint64_t session, std::string_view message) {
    if (sessions_.find(session) == sessions_.end()) return error("unknown_session");
    if (message.empty() || message.size() > maxDebugMessageBytes) return error("message_size");
    for (unsigned char ch : message) if (ch < 32 || ch > 126) return error("ascii_single_line_required");
    const auto args = tokens(message);
    if (args.size() < 2 || args[0] != "GAME/1") return error("expected_GAME/1");
    const auto command = args[1];
    const auto arity = [&](std::size_t size) { return args.size() == size; };
    try {
        if (command == "PING") return arity(2) ? ProtocolReply{"GAME/1 OK PONG", false} : error("PING_arguments");
        if (command == "QUIT") return arity(2) ? ProtocolReply{"GAME/1 OK BYE", true} : error("QUIT_arguments");
        if (command == "CREATE") {
            if (!arity(4) || !identifier(args[2]) || !identifier(args[3], 32)) return error("CREATE_archetype_name");
            if (entityFor(session)) return error("character_already_created");
            const auto archetype = world_.content().characters.find(std::string(args[2]));
            if (archetype == world_.content().characters.end() || !archetype->second.tags.count("player"))
                return error("archetype_not_playable");
            const auto map = world_.content().maps.find("arena");
            if (map == world_.content().maps.end()) return error("arena_missing");
            const Position position{std::min(100.0, map->second.width), std::min(100.0, map->second.height)};
            const EntityId id = world_.spawn(archetype->first, map->first, 1, position, std::string(args[3]));
            sessions_.at(session) = id;
            knownMaps_[id] = map->first;
            return {"GAME/1 OK CREATED id=" + std::to_string(id), false};
        }
        if (command == "TICK") {
            if (!console_) return error("TICK_console_only");
            std::uint64_t delta = 0;
            if (!arity(3) || !number(args[2], delta) || delta == 0 || delta > 60000 ||
                delta > std::numeric_limits<Milliseconds>::max() - world_.now()) return error("TICK_range_1_60000");
            world_.advance(world_.now() + delta);
            return {"GAME/1 OK TICK now=" + std::to_string(world_.now()), false};
        }
        const EntityId owner = entityFor(session);
        const Entity* entity = world_.find(owner);
        if (!entity) return error("CREATE_required");
        if (command == "MOVE") {
            Position destination;
            if (!arity(4) || !real(args[2], destination.x) || !real(args[3], destination.y)) return error("MOVE_x_y");
            const auto result = world_.move(owner, destination);
            return result ? ProtocolReply{"GAME/1 OK MOVED", false} : error(result.message);
        }
        if (command == "CAST") {
            std::uint64_t target = 0;
            if (!arity(4) || !identifier(args[2]) || !number(args[3], target) || target == 0) return error("CAST_skill_target");
            const auto result = world_.cast(owner, std::string(args[2]), target);
            return result ? ProtocolReply{"GAME/1 OK CAST", false} : error(result.message);
        }
        if (command == "STATE") {
            if (!arity(2)) return error("STATE_arguments");
            std::string response = "GAME/1 OK STATE now=" + std::to_string(world_.now()) + "\n" + entityLine(world_, *entity);
            for (const auto& effect : entity->effects) {
                const auto line = "\nEFFECT id=" + clean(effect.definition) + " stacks=" + std::to_string(effect.stacks) +
                    " expires=" + std::to_string(effect.expiresAt);
                if (response.size() + line.size() + 32 > maxDebugMessageBytes) { response += "\nMORE effects_omitted=1"; break; }
                response += line;
            }
            return {response, false};
        }
        if (command == "ENTITIES") {
            if (!arity(2)) return error("ENTITIES_arguments");
            std::string response = "GAME/1 OK ENTITIES";
            std::size_t omitted = 0;
            for (const auto& pair : world_.entities()) {
                if (pair.second.map != entity->map) continue;
                const auto line = "\n" + entityLine(world_, pair.second);
                if (response.size() + line.size() + 64 > maxDebugMessageBytes) { ++omitted; continue; }
                response += line;
            }
            if (omitted) response += "\nMORE entities_omitted=" + std::to_string(omitted);
            return {response, false};
        }
        return error("unknown_command");
    } catch (const std::invalid_argument& exception) {
        return error(exception.what());
    }
}
std::vector<EventFrame> DebugProtocol::takeEventFrames() {
    for (const auto& pair : world_.entities()) knownMaps_[pair.first] = pair.second.map;
    std::vector<EventFrame> result;
    for (const auto& event : world_.takeEvents()) {
        auto map = knownMaps_.find(event.target);
        if (map == knownMaps_.end()) map = knownMaps_.find(event.source);
        if (map == knownMaps_.end()) continue;
        std::ostringstream line;
        line.imbue(std::locale::classic());
        line << std::setprecision(10) << "\nEVENT kind=" << clean(event.kind) << " source=" << event.source
            << " target=" << event.target << " content=" << clean(event.contentId) << " value=" << event.value;
        const auto text = line.str();
        if (result.empty() || result.back().map != map->second ||
            result.back().text.size() + text.size() > maxDebugMessageBytes)
            result.push_back({map->second, "GAME/1 EVENTS"});
        result.back().text += text;
    }
    for (auto it = knownMaps_.begin(); it != knownMaps_.end();) {
        if (!world_.find(it->first)) it = knownMaps_.erase(it); else ++it;
    }
    return result;
}
} // namespace game::net
