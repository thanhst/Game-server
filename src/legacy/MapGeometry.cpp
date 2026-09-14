#include "game/legacy/MapGeometry.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace game::legacy {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(std::string("HUNR geometry: ") + message);
}

std::int32_t integer(const nlohmann::json& value) {
    require(value.is_number_integer(), "tile configuration must contain integers");
    if (value.is_number_unsigned())
        require(value.get<std::uint64_t>() <= static_cast<std::uint64_t>((std::numeric_limits<std::int32_t>::max)()),
                "tile configuration exceeds Java int");
    const auto result = value.get<std::int64_t>();
    require(result >= (std::numeric_limits<std::int32_t>::min)() && result <= (std::numeric_limits<std::int32_t>::max)(),
            "tile configuration exceeds Java int");
    return static_cast<std::int32_t>(result);
}

Bytes readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) throw std::runtime_error("Cannot open external map data: " + path.string());
    const auto length = input.tellg();
    // Two one-byte dimensions bound a valid grid to 65,025 cells. Preserve
    // surplus source bytes, while bounding malformed external file allocations.
    if (length < 0 || length > 1024 * 1024) throw std::runtime_error("Invalid external map file size: " + path.string());
    Bytes bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot read external map data: " + path.string());
    return bytes;
}

std::int16_t narrowShort(std::int32_t value) noexcept {
    const auto bits = static_cast<std::uint32_t>(value) & 0xffffU;
    return static_cast<std::int16_t>(bits >= 0x8000U ? static_cast<std::int32_t>(bits) - 65536 : static_cast<std::int32_t>(bits));
}
} // namespace

TileSetCatalog TileSetCatalog::fromContent(const ContentSnapshot& content) {
    for (std::size_t i = 0; i < content.table("nr_others").size(); ++i)
        if (content.table("nr_others")[i].at("key") == "tile_set")
            return fromJson(content.embeddedRow("nr_others", i).at("value"));
    throw std::invalid_argument("HUNR geometry: nr_others.tile_set is missing");
}

TileSetCatalog TileSetCatalog::fromJson(const nlohmann::json& tileSet) {
    const auto& types = tileSet.at("tile_type");
    const auto& indices = tileSet.at("tile_index");
    require(types.is_array() && indices.is_array() && types.size() == indices.size(), "tile set dimensions differ");
    TileSetCatalog result;
    result.masks_.resize(types.size());
    for (std::size_t set = 0; set < types.size(); ++set) {
        result.masks_[set].fill(0);
        require(types[set].is_array() && indices[set].is_array() && types[set].size() == indices[set].size(),
                "tile set group dimensions differ");
        for (std::size_t group = 0; group < types[set].size(); ++group) {
            const auto mask = integer(types[set][group]);
            require(indices[set][group].is_array(), "tile group must be an array");
            for (const auto& cell : indices[set][group]) {
                const auto tile = integer(cell);
                // TMap reads tile bytes as unsigned 0..255; impossible indices
                // simply never match in the Java loop, and remain in raw content.
                if (tile >= 0 && tile <= 255) result.masks_[set][static_cast<std::size_t>(tile)] |= mask;
            }
        }
    }
    return result;
}

std::int32_t TileSetCatalog::classify(std::int32_t tileSetId, std::uint8_t tile) const {
    require(tileSetId >= 1 && static_cast<std::size_t>(tileSetId) <= masks_.size(), "tileSetId must index a nonzero known set");
    return masks_[static_cast<std::size_t>(tileSetId - 1)][tile];
}

MapGeometry MapGeometry::load(const std::filesystem::path& externalMapDirectory,
                              std::int32_t mapId, std::int32_t tileSetId, const TileSetCatalog& tileSets) {
    require(mapId >= 0, "map ID must be nonnegative");
    const auto name = std::to_string(mapId);
    const auto blockFile = externalMapDirectory / "block" / name;
    const auto blockData = std::filesystem::exists(blockFile) ? readFile(blockFile) : Bytes{};
    return fromBytes(mapId, tileSetId, readFile(externalMapDirectory / name), blockData, tileSets);
}

MapGeometry MapGeometry::fromBytes(std::int32_t mapId, std::int32_t tileSetId, Bytes mapData,
                                   const Bytes& blockData, const TileSetCatalog& tileSets) {
    require(mapId >= 0, "map ID must be nonnegative");
    require(mapData.size() >= 2, "map data needs width and height bytes");
    MapGeometry result;
    result.mapId_ = mapId;
    result.tileSetId_ = tileSetId;
    result.tilesWide_ = mapData[0];
    result.tilesHigh_ = mapData[1];
    require(result.tilesWide_ > 0 && result.tilesHigh_ > 0, "map dimensions must be nonzero");
    const auto area = static_cast<std::size_t>(result.tilesWide_) * static_cast<std::size_t>(result.tilesHigh_);
    const auto sourceCells = mapData.size() - 2;
    require(sourceCells >= area, "map tile array is shorter than width times height");
    require(blockData.size() <= sourceCells, "block array exceeds map tile storage");
    result.types_.assign(sourceCells, 0);
    result.blocked_.assign(sourceCells, false);
    for (std::size_t i = 0; i < area; ++i) result.types_[i] = tileSets.classify(tileSetId, mapData[i + 2]);
    for (std::size_t i = 0; i < blockData.size(); ++i) result.blocked_[i] = blockData[i] == 1;
    // Java setListFloor iterates X then Y. This also defines closest-floor tie order.
    for (std::int32_t x = 0; x < result.tilesWide_; ++x) {
        for (std::int32_t y = 0; y < result.tilesHigh_; ++y) {
            const auto index = static_cast<std::size_t>(y * result.tilesWide_ + x);
            if ((result.types_[index] & Top) == Top || (result.types_[index] & Bridge) == Bridge)
                result.floors_.push_back(TileFloor{x, y});
        }
    }
    result.mapData_ = std::move(mapData);
    return result;
}

bool MapGeometry::containsPixel(std::int32_t x, std::int32_t y) const noexcept {
    return x >= 0 && y >= 0 && x < widthPixels() && y < heightPixels();
}

std::int64_t MapGeometry::pixelIndex(std::int32_t x, std::int32_t y) const noexcept {
    // C++ signed division truncates toward zero, matching Java.
    return static_cast<std::int64_t>(y / TileSize) * tilesWide_ + x / TileSize;
}

std::int32_t MapGeometry::tileAt(std::int32_t tileX, std::int32_t tileY) const noexcept {
    const auto index = static_cast<std::int64_t>(tileY) * tilesWide_ + tileX;
    if (mapData_.size() < 2 || index < 0 || static_cast<std::uint64_t>(index) >= mapData_.size() - 2) return JavaOutOfBounds;
    return mapData_[static_cast<std::size_t>(index) + 2];
}

std::int32_t MapGeometry::tileAtPixel(std::int32_t x, std::int32_t y) const noexcept {
    const auto index = pixelIndex(x, y);
    if (mapData_.size() < 2 || index < 0 || static_cast<std::uint64_t>(index) >= mapData_.size() - 2) return JavaOutOfBounds;
    return mapData_[static_cast<std::size_t>(index) + 2];
}

std::int32_t MapGeometry::tileTypeAtPixel(std::int32_t x, std::int32_t y) const noexcept {
    const auto index = pixelIndex(x, y);
    if (x < 0 || y < 0 || index < 0 || static_cast<std::uint64_t>(index) >= types_.size()) return JavaOutOfBounds;
    return types_[static_cast<std::size_t>(index)];
}

bool MapGeometry::checkBlock(std::int32_t x, std::int32_t y) const {
    const auto index = pixelIndex(x, y);
    if (index < 0 || static_cast<std::uint64_t>(index) >= blocked_.size()) throw std::out_of_range("HUNR block coordinate is outside tile storage");
    return blocked_[static_cast<std::size_t>(index)];
}

std::optional<TileFloor> MapGeometry::closestFloor(std::int32_t x, std::int32_t y) const {
    std::optional<TileFloor> closest;
    std::int64_t minimum = (std::numeric_limits<std::int64_t>::max)();
    for (const auto& floor : floors_) {
        const auto dx = static_cast<double>(floor.x) - x / TileSize;
        const auto dy = static_cast<double>(floor.y) - y / TileSize;
        const auto distance = static_cast<std::int64_t>(std::sqrt(dx * dx + dy * dy));
        if (distance < minimum) { minimum = distance; closest = floor; }
    }
    return closest;
}

std::int16_t MapGeometry::legacyCollisionLand(std::int16_t x, std::int16_t y) const {
    y = narrowShort(y / TileSize * TileSize);
    for (std::size_t iteration = 0; iteration < 8192; ++iteration) {
        const auto type = tileTypeAtPixel(x, y);
        if (((type & Top) == Top || (type & Bridge) == Bridge) && !checkBlock(x, y)) return y;
        y = narrowShort(static_cast<std::int32_t>(y) + TileSize);
        if (y >= widthPixels()) return 24; // Actual Java width comparison, not height.
    }
    throw std::logic_error("HUNR collisionLand exceeded a complete Java short cycle");
}

Packet MapGeometry::templatePacket(bool extended) const {
    PacketWriter writer;
    writer.writeByte(extended ? 11 : 10);
    if (extended) writer.writeInt(mapId_);
    return writer.writeBytes(mapData_).packet(-28);
}

} // namespace game::legacy
