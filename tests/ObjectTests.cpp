#include "game/object/Object.h"
#include "game/object/WorldObject.h"
#include "game/resident/Creature.h"
#include "game/resident/Player.h"

#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Operation>
void rejectsInvalid(Operation&& operation, const char* message) {
    try {
        operation();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(message);
}

game::ResidentInit playerInit() {
    return {{1}, "hero", "arena", {10, 20}, game::Vitality(100), game::Mana(50)};
}

void residentHierarchy() {
    static_assert(std::is_base_of_v<game::Object, game::WorldObject>);
    static_assert(std::is_base_of_v<game::WorldObject, game::Resident>);
    static_assert(std::is_base_of_v<game::Resident, game::Player>);
    static_assert(std::is_base_of_v<game::Resident, game::Creature>);
    static_assert(!std::is_copy_constructible_v<game::Object>);
    static_assert(std::has_virtual_destructor_v<game::Object>);

    game::Player player{playerInit()};
    const game::Object& object = player;
    const game::WorldObject& spatial = player;
    check(object.id() == game::ObjectId{1} && object.kind() == game::ObjectKind::Player,
        "player identity");
    check(spatial.mapInstanceId() == "arena" && spatial.position().x == 10 && spatial.position().y == 20,
        "player world location");
    auto positionCopy = spatial.position();
    positionCopy.x = 999;
    check(spatial.position().x == 10, "position accessor exposed mutable world state");
    check(player.definition() == "hero" && player.isAlive(), "player definition and life");
    check(player.receiveDamage(30).applied == 30 && player.vitality().current() == 70,
        "resident owns damage behavior");
    check(!player.spendMana(60) && player.mana().current() == 50,
        "resident rejects unaffordable mana cost");
    check(player.spendMana(20) && player.mana().current() == 30,
        "resident spends mana");
    check(player.receiveDamage(70).died && !player.isAlive(), "resident death");
    check(player.receiveHealing(10) == 0 && !player.isAlive(), "healing did not revive resident");
    check(player.revive(25) && player.vitality().current() == 25, "resident explicit revive");

    game::Creature creature({{2}, "wolf", "forest", {0, 0},
        game::Vitality(80), game::Mana(0)});
    check(creature.kind() == game::ObjectKind::Creature && creature.mana().maximum() == 0,
        "creature has its own kind and resources");
}

void invalidObjectState() {
    rejectsInvalid([] {
        auto init = playerInit();
        init.id = {};
        game::Player invalid(init);
    }, "zero object id accepted");
    rejectsInvalid([] {
        auto init = playerInit();
        init.mapInstanceId.clear();
        game::Player invalid(init);
    }, "empty map accepted");
    rejectsInvalid([] {
        auto init = playerInit();
        init.position.x = std::numeric_limits<double>::quiet_NaN();
        game::Player invalid(init);
    }, "nonfinite position accepted");
    rejectsInvalid([] {
        auto init = playerInit();
        init.definition.clear();
        game::Player invalid(init);
    }, "empty resident definition accepted");
    rejectsInvalid([] {
        auto init = playerInit();
        init.bodyRadius = 0;
        game::Player invalid(init);
    }, "zero resident body radius accepted");
}
} // namespace

void runObjectTests() {
    residentHierarchy();
    invalidObjectState();
}
