#include "game/legacy/MapGeometry.h"

#include <functional>
#include <stdexcept>
#include <string>

#ifndef GAME_HUNR_CONTENT_FILE
#define GAME_HUNR_CONTENT_FILE "content/hunr/content.json"
#endif

namespace {
void checkGeometry(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(std::string("MapGeometryTests: ") + message);
}
void rejectsGeometry(const std::function<void()>& operation) {
    try { operation(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error("MapGeometryTests: invalid geometry accepted");
}
} // namespace

void runMapGeometryTests() {
    using namespace game::legacy;
    const auto tileSets = TileSetCatalog::fromJson(nlohmann::json::parse(
        R"({"tile_type":[[2,4,1024]],"tile_index":[[[1],[1],[2]]]})"));
    checkGeometry(tileSets.classify(1, 1) == 6 && tileSets.classify(1, 2) == 1024 && tileSets.classify(1, 255) == 0,
                  "tile masks OR across matching groups");
    const Bytes source{3, 3, 0, 0, 2, 1, 2, 0, 1, 0, 255};
    const auto geometry = MapGeometry::fromBytes(42, 1, source, Bytes{0, 0, 0, 1, 2}, tileSets);
    checkGeometry(geometry.tilesWide() == 3 && geometry.tilesHigh() == 3 && geometry.widthPixels() == 72 && geometry.heightPixels() == 72,
                  "unsigned header dimensions use 24 pixel tiles");
    checkGeometry(geometry.containsPixel(71, 71) && !geometry.containsPixel(72, 0) && !geometry.containsPixel(-1, 0),
                  "strict network movement bounds");
    checkGeometry(geometry.tileAt(2, 2) == 255, "tile ID is unsigned byte");
    checkGeometry(geometry.tileTypeAtPixel(0, 24) == 6 && geometry.tileTypeAtPixel(24, 24) == 1024,
                  "tile set selection by one-based tileSetId");
    checkGeometry(geometry.checkBlock(0, 24) && !geometry.checkBlock(24, 24) && !geometry.checkBlock(0, 48),
                  "block bytes equal one only and missing bytes default false");
    checkGeometry(geometry.floors().size() == 4 && geometry.floors()[0].x == 0 && geometry.floors()[0].y == 1 &&
                  geometry.floors()[1].x == 0 && geometry.floors()[1].y == 2,
                  "Java column-major floor order includes blocked floor cells");
    const auto closest = geometry.closestFloor(24, 0);
    checkGeometry(closest && closest->x == 0 && closest->y == 1,
                  "floor distance is truncated before comparison and tie keeps first source order");
    checkGeometry(geometry.legacyCollisionLand(0, 0) == 48, "blocked floor skipped during collisionLand");
    checkGeometry(geometry.legacyCollisionLand(24, 0) == 24, "bridge acts as floor");
    checkGeometry(geometry.tileTypeAtPixel(-1, 0) == 1000 && geometry.tileAtPixel(-1, 0) == 0,
                  "Java negative coordinate distinction preserved");
    checkGeometry(geometry.tileAtPixel(72, 0) == 1 && geometry.tileTypeAtPixel(72, 0) == 6,
                  "Java flattened index wrap is explicit; containsPixel must guard movement");
    checkGeometry(geometry.tileAt(99, 99) == 1000 && geometry.tileTypeAtPixel(999, 999) == 1000,
                  "Java tile out-of-storage sentinel");
    rejectsGeometry([&] { geometry.checkBlock(999, 999); });

    // Tall, narrow map: Java uses width 24 as its falling cutoff, even though a
    // real floor is at y=72. Captures the original behavior for legacy adapters.
    const auto tall = MapGeometry::fromBytes(43, 1, Bytes{1, 4, 0, 0, 0, 1}, {}, tileSets);
    checkGeometry(tall.legacyCollisionLand(0, 0) == 24, "legacy width-bound collisionLand behavior");
    const auto empty = MapGeometry::fromBytes(44, 1, Bytes{1, 1, 0}, {}, tileSets);
    checkGeometry(!empty.closestFloor(0, 0), "map with no floor has no closest floor");

    const auto packet = geometry.templatePacket();
    PacketReader normal(packet.payload);
    checkGeometry(packet.command == -28 && normal.readByte() == 10 && normal.readBytes(normal.remaining()) == source,
                  "map template packet retains exact external bytes");
    const auto extendedPacket = geometry.templatePacket(true);
    PacketReader extended(extendedPacket.payload);
    checkGeometry(extendedPacket.command == -28 && extended.readByte() == 11 && extended.readInt() == 42 &&
                  extended.readBytes(extended.remaining()) == source, "extended map template prepends int map ID");
    const auto surplus = MapGeometry::fromBytes(45, 1, Bytes{1, 1, 1, 255}, {}, tileSets);
    checkGeometry(surplus.mapData().size() == 4 && surplus.tileAt(1, 0) == 255 && surplus.tileTypeAtPixel(24, 0) == 0,
                  "surplus source bytes retained while tile classification covers declared grid only");
    rejectsGeometry([&] { MapGeometry::fromBytes(1, 1, Bytes{2, 2, 1}, {}, tileSets); });
    rejectsGeometry([&] { MapGeometry::fromBytes(1, 1, Bytes{0, 1, 1}, {}, tileSets); });
    rejectsGeometry([&] { MapGeometry::fromBytes(1, 0, Bytes{1, 1, 1}, {}, tileSets); });
    rejectsGeometry([&] { MapGeometry::fromBytes(1, 1, Bytes{1, 1, 1}, Bytes{0, 0}, tileSets); });
    rejectsGeometry([&] { TileSetCatalog::fromJson(nlohmann::json::parse(R"({"tile_type":[[2]],"tile_index":[[]]})")); });

    const auto content = ContentSnapshot::load(GAME_HUNR_CONTENT_FILE);
    const auto sourceSets = TileSetCatalog::fromContent(content);
    checkGeometry(sourceSets.size() == 33 && sourceSets.classify(1, 28) == (8192 | 4 | 8),
                  "actual nr_others tile set definitions loaded");
}
