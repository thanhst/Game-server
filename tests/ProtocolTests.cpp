#include "game/net/DebugProtocol.h"

#include <stdexcept>
#include <string>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
game::Content protocolContent() {
    game::Content content;
    game::MapDefinition map;
    map.id = "arena";
    map.width = 200;
    map.height = 200;
    content.maps.emplace(map.id, map);
    game::EffectDefinition effect;
    effect.id = "hit";
    effect.handler = "damage";
    effect.magnitude = 10;
    content.effects.emplace(effect.id, effect);
    game::SkillDefinition skill;
    skill.id = "strike";
    skill.name = "Strike";
    skill.range = 100;
    skill.effects = {"hit"};
    content.skills.emplace(skill.id, skill);
    game::CharacterDefinition player;
    player.id = "hero";
    player.name = "Hero";
    player.attributes = {{"hp", 100}, {"mana", 100}, {"attack", 10}, {"defense", 0}, {"speed", 10}};
    player.tags = {"player"};
    player.skills = {"strike"};
    content.characters.emplace(player.id, player);
    auto mob = player;
    mob.id = "mob";
    mob.tags = {"mob"};
    content.characters.emplace(mob.id, mob);
    return content;
}
bool rejected(const game::net::ProtocolReply& reply) { return reply.text.find("GAME/1 ERROR ") == 0; }
} // namespace

void runProtocolTests() {
    game::World world(protocolContent());
    const auto mob = world.spawn("mob", "arena", 2, {105, 100}, "Target");
    game::net::DebugProtocol protocol(world);
    protocol.connect(10);
    protocol.connect(20);
    expect(rejected(protocol.dispatch(30, "GAME/1 PING")), "unknown session accepted");
    expect(rejected(protocol.dispatch(10, "PING")), "unversioned input accepted");
    expect(rejected(protocol.dispatch(10, "GAME/2 PING")), "unsupported version accepted");
    expect(rejected(protocol.dispatch(10, "GAME/1 PING extra")), "trailing input accepted");
    expect(rejected(protocol.dispatch(10, "GAME/1 PING\nQUIT")), "multiple commands accepted");
    expect(rejected(protocol.dispatch(10, std::string(4097, 'x'))), "oversized frame accepted");
    expect(rejected(protocol.dispatch(10, "GAME/1 CREATE mob Enemy")), "non-player archetype accepted");
    expect(!rejected(protocol.dispatch(10, "GAME/1 CREATE hero First")), "valid creation failed");
    expect(!rejected(protocol.dispatch(20, "GAME/1 CREATE hero Second")), "second creation failed");
    const auto first = protocol.entityFor(10);
    const auto second = protocol.entityFor(20);
    expect(first && second && first != second, "sessions share identity");
    expect(rejected(protocol.dispatch(10, "GAME/1 CREATE hero Replacement")), "second character accepted");
    expect(rejected(protocol.dispatch(10, "GAME/1 MOVE 110 100 999")), "spoofed owner accepted");
    expect(rejected(protocol.dispatch(10, "GAME/1 MOVE nan 100")), "NaN coordinate accepted");
    expect(rejected(protocol.dispatch(10, "GAME/1 MOVE 1x 100")), "partial numeric coordinate accepted");
    expect(rejected(protocol.dispatch(10, "GAME/1 CAST strike 18446744073709551616")), "overflowing target accepted");
    expect(rejected(protocol.dispatch(10, "GAME/1 CAST strike -1")), "negative target accepted");
    expect(rejected(protocol.dispatch(10, "GAME/1 TICK 1000")), "network advanced simulation");
    expect(!rejected(protocol.dispatch(10, "GAME/1 CAST strike " + std::to_string(mob))), "valid cast failed");
    expect(world.find(mob)->hp < 100, "cast did not mutate target");
    expect(world.find(second)->hp == 100, "cast mutated another owned character");
    const auto frames = protocol.takeEventFrames();
    expect(!frames.empty(), "game events missing");
    for (const auto& frame : frames) {
        expect(frame.text.size() <= game::net::maxDebugMessageBytes, "oversized event output");
        expect(protocol.canSee(10, frame.map), "own map event hidden");
        expect(!protocol.canSee(30, frame.map), "unauthorized observer sees map event");
    }
    expect(protocol.dispatch(10, "GAME/1 STATE").text.find("max_hp=100") != std::string::npos, "state maximum incorrect");
    protocol.disconnect(10);
    expect(!world.find(first) && world.find(second), "disconnect removed wrong character");
    expect(rejected(protocol.dispatch(10, "GAME/1 STATE")), "closed session accepted");
    game::net::DebugProtocol console(world, true);
    console.connect(1);
    expect(rejected(console.dispatch(1, "GAME/1 TICK 60001")), "unbounded console tick accepted");
    expect(!rejected(console.dispatch(1, "GAME/1 TICK 1000")), "console tick failed");
    expect(world.now() == 1000, "console tick did not advance world clock");
    expect(console.dispatch(1, "GAME/1 QUIT").close, "QUIT did not request close");
}
