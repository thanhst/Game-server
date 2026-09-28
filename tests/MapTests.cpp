#include "game/resident/Player.h"
#include "game/world/MapInstance.h"

#include <limits>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void mapMembershipAndMovement() {
    game::MapInstance arena("arena#1", {"arena", 100, 80});
    game::Player player({{1}, "hero", "arena#1", {10, 20},
        game::Vitality(100), game::Mana(50)});

    check(arena.enter(player) == game::MapResult::Ok && arena.has(player.id()),
        "resident did not enter map");
    check(arena.enter(player) == game::MapResult::AlreadyPresent,
        "duplicate map registration accepted");
    check(arena.move(player, {99.5, 79.5}) == game::MapResult::Ok,
        "resident body at map boundary rejected");
    check(arena.move(player, {100, 80}) == game::MapResult::OutOfBounds &&
        player.position().x == 99.5 && player.position().y == 79.5,
        "out-of-bounds move mutated position");
    check(arena.move(player, {std::numeric_limits<double>::quiet_NaN(), 0}) ==
        game::MapResult::OutOfBounds && player.position().x == 99.5,
        "nonfinite move mutated position");
    check(arena.leave(player) == game::MapResult::Ok && !arena.has(player.id()),
        "resident did not leave map");
    check(arena.move(player, {1, 1}) == game::MapResult::NotPresent,
        "unregistered resident moved");
}

void mapTransfer() {
    game::MapInstance arena("arena#1", {"arena", 100, 80});
    game::MapDefinition dungeonDefinition{"dungeon", 40, 40};
    dungeonDefinition.walls.push_back(game::ConvexPolygon::rectangle(20, 20, 5, 5));
    game::MapInstance dungeon("dungeon#1", dungeonDefinition);
    game::MapInstance duplicateInstance("arena#1", {"arena", 100, 80});
    game::Player player({{2}, "hero", "arena#1", {10, 20},
        game::Vitality(100), game::Mana(50)});
    check(arena.enter(player) == game::MapResult::Ok, "transfer setup");

    check(arena.transferTo(player, duplicateInstance, {1, 1}) ==
        game::MapResult::InstanceIdConflict && arena.has(player.id()),
        "two map instances with the same ID accepted a transfer");

    check(arena.transferTo(player, dungeon, {41, 1}) == game::MapResult::OutOfBounds &&
        arena.has(player.id()) && !dungeon.has(player.id()) && player.mapInstanceId() == "arena#1",
        "rejected transfer changed map membership");
    check(arena.transferTo(player, dungeon, {22, 22}) == game::MapResult::Blocked &&
        arena.has(player.id()) && !dungeon.has(player.id()) && player.mapInstanceId() == "arena#1",
        "transfer into destination wall changed map membership");
    check(arena.transferTo(player, dungeon, {5, 6}) == game::MapResult::Ok &&
        !arena.has(player.id()) && dungeon.has(player.id()) &&
        player.mapInstanceId() == "dungeon#1" && player.position().x == 5 && player.position().y == 6,
        "successful transfer did not update object and maps together");
    check(arena.move(player, {7, 7}) == game::MapResult::WrongMap,
        "old map still controls transferred resident");
    check(dungeon.transferTo(player, dungeon, {7, 8}) == game::MapResult::Ok &&
        player.position().x == 7 && player.position().y == 8,
        "same-map transfer did not move resident");
}

void wallCollisionExample() {
    game::MapDefinition definition{"arena", 100, 80};
    definition.walls.push_back(game::ConvexPolygon::rectangle(40, 0, 2, 50));
    game::MapInstance arena("arena#1", definition);
    game::Player player({{3}, "hero", "arena#1", {10, 20},
        game::Vitality(100), game::Mana(50)});
    check(arena.enter(player) == game::MapResult::Ok, "wall fixture spawn failed");
    check(arena.move(player, {30, 20}) == game::MapResult::Ok,
        "free movement before wall rejected");
    check(arena.move(player, {40, 20}) == game::MapResult::Blocked &&
        player.position().x == 30, "movement into wall accepted");
    check(arena.move(player, {50, 20}) == game::MapResult::Blocked &&
        player.position().x == 30, "single step tunneled through wall");
    check(arena.move(player, {30, 55}) == game::MapResult::Ok &&
        arena.move(player, {50, 55}) == game::MapResult::Ok,
        "walking around the end of the wall was rejected");

    game::Player insideWall({{4}, "hero", "arena#1", {41, 20},
        game::Vitality(100), game::Mana(50)});
    check(arena.enter(insideWall) == game::MapResult::Blocked,
        "resident spawned inside wall");

    game::Player nearCorner({{5}, "hero", "arena#1", {39.6, 50.4},
        game::Vitality(100), game::Mana(50)});
    check(arena.enter(nearCorner) == game::MapResult::Ok &&
        arena.move(nearCorner, {39.5, 50.5}) == game::MapResult::Ok,
        "clear path beside rounded wall corner was blocked");
}

void polygonWallExample() {
    game::MapDefinition definition{"arena", 100, 80};
    definition.walls.push_back({{{40, 10}, {60, 20}, {40, 30}}});
    game::MapInstance arena("arena#1", definition);

    game::Player crossing({{6}, "hero", "arena#1", {30, 20},
        game::Vitality(100), game::Mana(50)});
    check(arena.enter(crossing) == game::MapResult::Ok &&
        arena.move(crossing, {70, 20}) == game::MapResult::Blocked &&
        crossing.position().x == 30,
        "movement tunneled through a triangular wall");

    // (55, 12) is inside the triangle's bounding rectangle, not its geometry.
    game::Player outsideSlope({{7}, "hero", "arena#1", {55, 12},
        game::Vitality(100), game::Mana(50)});
    check(arena.enter(outsideSlope) == game::MapResult::Ok &&
        arena.move(outsideSlope, {58, 12}) == game::MapResult::Ok,
        "triangle's empty bounding-box area was blocked");
}

void invalidMapDefinition() {
    bool rejected = false;
    try { game::MapInstance invalid("arena#1", {"arena", 0, 80}); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "zero map width accepted");
    rejected = false;
    try {
        game::MapDefinition invalid{"arena", 100, 80};
        invalid.walls.push_back(game::ConvexPolygon::rectangle(99, 1, 2, 5));
        game::MapInstance map("arena#1", invalid);
    } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "wall extending outside map accepted");
    rejected = false;
    try {
        game::MapDefinition invalid{"arena", 100, 80};
        invalid.walls.push_back({{{10, 10}, {30, 10}, {20, 20}, {30, 30}, {10, 30}}});
        game::MapInstance map("arena#1", invalid);
    } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "concave wall accepted as one convex polygon");
}
} // namespace

void runMapTests() {
    mapMembershipAndMovement();
    mapTransfer();
    wallCollisionExample();
    polygonWallExample();
    invalidMapDefinition();
}
