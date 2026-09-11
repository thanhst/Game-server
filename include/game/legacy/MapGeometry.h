#pragma once

#include "game/legacy/LegacyContent.h"
#include "game/legacy/Packet.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace game::legacy {

class TileSetCatalog final {
public:
    static TileSetCatalog fromContent(const ContentSnapshot& content);
    static TileSetCatalog fromJson(const nlohmann::json& tileSet);
    std::int32_t classify(std::int32_t tileSetId, std::uint8_t tile) const;
    std::size_t size() const noexcept { return masks_.size(); }
private:
    std::vector<std::array<std::int32_t, 256>> masks_;
};

struct TileFloor { std::int32_t x = 0, y = 0; };

class MapGeometry final {
public:
    static constexpr std::int32_t TileSize = 24;
    static constexpr std::int32_t Top = 2;
    static constexpr std::int32_t Bridge = 1024;
    static constexpr std::int32_t JavaOutOfBounds = 1000;

    // externalMapDirectory contains <mapId> and optional block/<mapId> files.
    // It is deployment data, not a resource directory committed to this project.
    static MapGeometry load(const std::filesystem::path& externalMapDirectory,
                            std::int32_t mapId, std::int32_t tileSetId,
                            const TileSetCatalog& tileSets);
    static MapGeometry fromBytes(std::int32_t mapId, std::int32_t tileSetId,
                                 Bytes mapData, const Bytes& blockData,
                                 const TileSetCatalog& tileSets);

    std::int32_t mapId() const noexcept { return mapId_; }
    std::int32_t tileSetId() const noexcept { return tileSetId_; }
    std::int32_t tilesWide() const noexcept { return tilesWide_; }
    std::int32_t tilesHigh() const noexcept { return tilesHigh_; }
    std::int32_t widthPixels() const noexcept { return tilesWide_ * TileSize; }
    std::int32_t heightPixels() const noexcept { return tilesHigh_ * TileSize; }
    bool containsPixel(std::int32_t x, std::int32_t y) const noexcept;
    const Bytes& mapData() const noexcept { return mapData_; }
    const std::vector<TileFloor>& floors() const noexcept { return floors_; }

    // These methods retain TMap's flattened-array indexing/truncation semantics.
    // Call containsPixel when validating a network movement coordinate.
    std::int32_t tileAt(std::int32_t tileX, std::int32_t tileY) const noexcept;
    std::int32_t tileAtPixel(std::int32_t x, std::int32_t y) const noexcept;
    std::int32_t tileTypeAtPixel(std::int32_t x, std::int32_t y) const noexcept;
    bool checkBlock(std::int32_t x, std::int32_t y) const;
    std::optional<TileFloor> closestFloor(std::int32_t x, std::int32_t y) const;
    // Java collisionLand checks y against width rather than height. Kept explicit
    // so a future game's collision system does not unknowingly inherit that bug.
    std::int16_t legacyCollisionLand(std::int16_t x, std::int16_t y) const;

    Packet templatePacket(bool extended = false) const; // -28/sub10 or sub11+mapId

private:
    MapGeometry() = default;
    std::int64_t pixelIndex(std::int32_t x, std::int32_t y) const noexcept;
    std::int32_t mapId_ = 0, tileSetId_ = 0, tilesWide_ = 0, tilesHigh_ = 0;
    Bytes mapData_;
    std::vector<std::int32_t> types_;
    std::vector<bool> blocked_;
    std::vector<TileFloor> floors_;
};

} // namespace game::legacy
