#include "game/legacy/LegacyCaches.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace game::legacy {
namespace {
using Json = nlohmann::json;

void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(std::string("HUNR cache: ") + message);
}

std::int32_t integer(const Json& value) {
    if (value.is_string()) {
        const auto source = value.get<std::string>();
        const auto* begin = source.data();
        const auto* end = begin + source.size();
        if (begin != end && *begin == '+') ++begin;
        std::int32_t result = 0;
        const auto parsed = std::from_chars(begin, end, result);
        require(begin != end && parsed.ec == std::errc{} && parsed.ptr == end, "invalid JSONObject integer string");
        return result;
    }
    require(value.is_number_integer(), "expected integer");
    if (value.is_number_unsigned()) {
        require(value.get<std::uint64_t>() <= static_cast<std::uint64_t>((std::numeric_limits<std::int32_t>::max)()),
                "integer exceeds Java int");
        return static_cast<std::int32_t>(value.get<std::uint64_t>());
    }
    const auto result = value.get<std::int64_t>();
    require(result >= (std::numeric_limits<std::int32_t>::min)() && result <= (std::numeric_limits<std::int32_t>::max)(),
            "integer exceeds Java int");
    return static_cast<std::int32_t>(result);
}

std::int64_t longInteger(const Json& value) {
    require(value.is_number_integer(), "expected long integer");
    if (value.is_number_unsigned())
        require(value.get<std::uint64_t>() <= static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()),
                "integer exceeds Java long");
    return value.get<std::int64_t>();
}

std::int32_t count(std::size_t size, std::size_t maximum) {
    require(size <= maximum, "definition count exceeds this legacy client packet's wire field");
    return static_cast<std::int32_t>(size);
}

std::u16string utf16(std::string_view text) {
    std::u16string result;
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<std::uint8_t>(text[i++]);
        std::uint32_t cp = 0, minimum = 0;
        std::size_t tail = 0;
        if (first < 0x80U) cp = first;
        else if ((first & 0xe0U) == 0xc0U) { cp = first & 0x1fU; tail = 1; minimum = 0x80U; }
        else if ((first & 0xf0U) == 0xe0U) { cp = first & 0x0fU; tail = 2; minimum = 0x800U; }
        else if ((first & 0xf8U) == 0xf0U) { cp = first & 0x07U; tail = 3; minimum = 0x10000U; }
        else throw ProtocolError("Invalid UTF-8 content leading byte");
        if (tail > text.size() - i) throw ProtocolError("Truncated UTF-8 content");
        for (std::size_t part = 0; part < tail; ++part) {
            const auto next = static_cast<std::uint8_t>(text[i++]);
            if ((next & 0xc0U) != 0x80U) throw ProtocolError("Invalid UTF-8 content continuation");
            cp = (cp << 6U) | (next & 0x3fU);
        }
        if (cp < minimum || cp > 0x10ffffU || (cp >= 0xd800U && cp <= 0xdfffU))
            throw ProtocolError("Invalid UTF-8 content code point");
        if (cp <= 0xffffU) result.push_back(static_cast<char16_t>(cp));
        else {
            cp -= 0x10000U;
            result.push_back(static_cast<char16_t>(0xd800U + (cp >> 10U)));
            result.push_back(static_cast<char16_t>(0xdc00U + (cp & 0x3ffU)));
        }
    }
    return result;
}

void text(PacketWriter& writer, const std::string& value) {
    writer.writeModifiedUTF(utf16(value));
}

void text(PacketWriter& writer, const Json& value) {
    require(value.is_string(), "expected source string");
    text(writer, value.get<std::string>());
}

Packet notMap(std::int32_t subcommand, const Bytes& bytes) {
    return PacketWriter().writeByte(subcommand).writeBytes(bytes).packet(-28);
}

std::vector<std::vector<std::int32_t>> heads(const ContentSnapshot& content) {
    std::vector<std::vector<std::int32_t>> result;
    for (std::size_t i = 0; i < content.table("array_head_2_frames").size(); ++i) {
        const auto& row = content.embeddedRow("array_head_2_frames", i).at("data");
        require(row.is_array(), "head group must be an array");
        if (row.size() < 2) continue;
        const std::size_t start = integer(row[0]) < 0 ? 1U : 0U;
        if (row.size() - start < 2) continue;
        std::vector<std::int32_t> group;
        for (std::size_t k = start; k < row.size(); ++k) group.push_back(integer(row[k]));
        result.push_back(std::move(group));
    }
    return result;
}

std::vector<std::int32_t> itemIterationOrder(const ContentSnapshot& content) {
    // initItemTemplate uses new HashMap<Integer,...>(), then put in SQL order.
    // Simulate default load-factor resizing and bucket traversal. Existing bins
    // preserve insertion order through resize. Reject tree bins rather than
    // silently claiming their JVM-specific linked order is ID-sorted.
    std::vector<std::int32_t> result;
    std::size_t capacity = 16;
    auto hash = [](std::int32_t id) {
        const auto bits = static_cast<std::uint32_t>(id);
        return bits ^ (bits >> 16U);
    };
    for (const auto& row : content.table("nr_item")) {
        const auto id = integer(row.at("id"));
        require(id >= 0 && id <= 32767, "item ID must fit a nonnegative Java short");
        result.push_back(id);
        const auto bucket = hash(id) & (capacity - 1U);
        std::size_t binSize = 0;
        for (const auto previous : result) if ((hash(previous) & (capacity - 1U)) == bucket) ++binSize;
        if (binSize > 8) {
            require(capacity < 64, "Java HashMap tree-bin item order needs a dedicated compatibility profile");
            capacity *= 2;
        }
        if (result.size() > capacity * 3 / 4) capacity *= 2;
    }
    std::stable_sort(result.begin(), result.end(), [&](auto left, auto right) {
        return (hash(left) & (capacity - 1U)) < (hash(right) & (capacity - 1U));
    });
    return result;
}

Bytes mapCache(const ContentSnapshot& content, CacheVersions versions) {
    PacketWriter writer;
    writer.writeByte(versions.map).writeShort(count(content.table("nr_map").size(), 65535));
    for (const auto& row : content.table("nr_map")) text(writer, row.at("name"));
    writer.writeByte(count(content.table("nr_npc_template").size(), 255));
    for (const auto& row : content.table("nr_npc_template")) {
        const auto& npc = content.npcs().at(integer(row.at("id")));
        text(writer, npc.name);
        writer.writeShort(npc.head).writeShort(npc.body).writeShort(npc.leg);
        writer.writeByte(count(npc.menu.size(), 255));
        for (const auto& menu : npc.menu) {
            require(menu.is_array(), "NPC menu group must be an array");
            writer.writeByte(count(menu.size(), 255));
            for (const auto& label : menu) text(writer, label);
        }
    }
    writer.writeByte(count(content.table("nr_mob_template").size(), 255));
    for (const auto& row : content.table("nr_mob_template")) {
        const auto& mob = content.mobs().at(integer(row.at("id")));
        writer.writeByte(mob.id).writeByte(mob.type);
        text(writer, mob.name);
        writer.writeLong(mob.hp).writeByte(mob.rangeMove).writeByte(mob.speed).writeByte(mob.dartType);
    }
    return writer.take();
}

Bytes skillCache(const ContentSnapshot& content, CacheVersions versions) {
    PacketWriter writer;
    writer.writeByte(versions.skill).writeByte(count(content.table("nr_skill_option_template").size(), 255));
    for (const auto& row : content.table("nr_skill_option_template")) text(writer, row.at("name"));
    constexpr std::array<std::u16string_view, 7> names{{u"Trái đất", u"Namec", u"Xayda", u"Chưa xác định",
                                                      u"Chưa xác định", u"Chưa xác định", u"Chưa xác định"}};
    for (const auto& row : content.table("nr_skill"))
        require(integer(row.at("class")) >= 0 && integer(row.at("class")) < static_cast<std::int32_t>(names.size()),
                "class exceeds this Java server's seven-class cache profile");
    writer.writeByte(static_cast<std::int32_t>(names.size()));
    for (std::int32_t classId = 0; classId < static_cast<std::int32_t>(names.size()); ++classId) {
        std::vector<const SkillTemplate*> definitions;
        for (const auto& row : content.table("nr_skill"))
            if (integer(row.at("class")) == classId) definitions.push_back(&content.skill(classId, integer(row.at("skill_id"))));
        writer.writeModifiedUTF(names[static_cast<std::size_t>(classId)]).writeByte(count(definitions.size(), 255));
        for (const auto* definition : definitions) {
            writer.writeByte(static_cast<std::int32_t>(definition->id));
            text(writer, definition->name);
            writer.writeByte(definition->maxPoint).writeByte(definition->manaUseType).writeByte(definition->type).writeShort(definition->icon);
            text(writer, definition->damageInfo);
            text(writer, definition->description);
            writer.writeByte(count(definition->levels.size(), 255));
            for (const auto& level : definition->levels) {
                writer.writeShort(level.id).writeByte(level.point).writeLong(level.powerRequire).writeShort(level.manaUse)
                    .writeInt(level.cooldownMs).writeShort(level.dx).writeShort(level.dy).writeByte(level.maxFight)
                    .writeShort(level.damage).writeShort(level.price);
                text(writer, level.moreInfo);
            }
        }
    }
    return writer.take();
}

Bytes itemCache(const ContentSnapshot& content, std::uint8_t version, std::int32_t type,
                const std::vector<std::int32_t>& order) {
    PacketWriter writer;
    writer.writeByte(version).writeByte(type);
    constexpr std::size_t split = 800;
    if (type == 0) {
        writer.writeShort(count(content.table("nr_item_option_template").size(), 65535));
        for (const auto& row : content.table("nr_item_option_template")) {
            writer.writeShort(integer(row.at("id")));
            text(writer, row.at("name"));
            writer.writeByte(integer(row.at("type")));
        }
    } else {
        require(order.size() >= split, "Java item cache requires at least 800 definitions");
        writer.writeShort(static_cast<std::int32_t>(split));
        if (type == 2) writer.writeShort(count(order.size(), 65535));
        const auto begin = type == 1 ? 0U : split;
        const auto end = type == 1 ? split : order.size();
        for (auto i = begin; i < end; ++i) {
            const auto& item = content.items().at(order[i]);
            writer.writeShort(item.id).writeByte(item.type).writeByte(item.gender);
            text(writer, item.name);
            text(writer, item.description);
            require(item.powerRequire >= (std::numeric_limits<std::int32_t>::min)() &&
                    item.powerRequire <= (std::numeric_limits<std::int32_t>::max)(), "item requirement exceeds Java int");
            writer.writeByte(item.level).writeInt(static_cast<std::int32_t>(item.powerRequire))
                .writeShort(item.icon).writeShort(item.part).writeBoolean(item.upToUp);
        }
    }
    return writer.take();
}

void shortArray(PacketWriter& writer, const Json& array) {
    require(array.is_array(), "short vector must be an array");
    writer.writeShort(count(array.size(), 65535));
    for (const auto& value : array) writer.writeShort(integer(value));
}

Bytes dartCache(const ContentSnapshot& content) {
    PacketWriter writer;
    writer.writeShort(count(content.table("nr_dart").size(), 65535));
    for (std::size_t i = 0; i < content.table("nr_dart").size(); ++i) {
        const auto& row = content.table("nr_dart")[i];
        const auto& embedded = content.embeddedRow("nr_dart", i);
        for (const auto* key : {"id", "n_update", "va", "xd_percent"}) writer.writeShort(integer(row.at(key)));
        for (const auto* key : {"tail", "tail_border", "xd1", "xd2"}) shortArray(writer, embedded.at(key));
        for (const auto* key : {"head", "head_border"}) {
            const auto& array = embedded.at(key);
            writer.writeShort(count(array.size(), 65535));
            for (const auto& values : array) shortArray(writer, values);
        }
    }
    return writer.take();
}

Bytes arrowCache(const ContentSnapshot& content) {
    PacketWriter writer;
    writer.writeShort(count(content.table("nr_arrow").size(), 65535));
    for (std::size_t i = 0; i < content.table("nr_arrow").size(); ++i) {
        writer.writeShort(integer(content.table("nr_arrow")[i].at("id")));
        const auto& images = content.embeddedRow("nr_arrow", i).at("img");
        require(images.is_array() && images.size() >= 3, "arrow needs three images");
        for (std::size_t k = 0; k < 3; ++k) writer.writeShort(integer(images[k]));
    }
    return writer.take();
}

Bytes effectCache(const ContentSnapshot& content) {
    PacketWriter writer;
    writer.writeShort(count(content.table("nr_effect").size(), 65535));
    for (std::size_t i = 0; i < content.table("nr_effect").size(); ++i) {
        writer.writeShort(integer(content.table("nr_effect")[i].at("id")));
        const auto& info = content.embeddedRow("nr_effect", i).at("info");
        writer.writeByte(count(info.size(), 255));
        for (const auto& part : info)
            writer.writeShort(integer(part.at("id"))).writeByte(integer(part.at("dx"))).writeByte(integer(part.at("dy")));
    }
    return writer.take();
}

Bytes imageCache(const ContentSnapshot& content) {
    PacketWriter writer;
    writer.writeShort(count(content.table("nr_image").size(), 65535));
    for (std::size_t i = 0; i < content.table("nr_image").size(); ++i) {
        const auto& small = content.embeddedRow("nr_image", i).at("small_image");
        writer.writeByte(integer(small.at("id")));
        for (const auto* key : {"x", "y", "w", "h"}) writer.writeShort(integer(small.at(key)));
    }
    return writer.take();
}

Bytes partCache(const ContentSnapshot& content) {
    PacketWriter writer;
    writer.writeShort(count(content.table("nr_part").size(), 65535));
    constexpr std::array<std::size_t, 4> partSizes{{3, 17, 14, 2}}; // Part.java constructor
    for (std::size_t i = 0; i < content.table("nr_part").size(); ++i) {
        const auto type = integer(content.table("nr_part")[i].at("type"));
        require(type >= 0 && type < static_cast<std::int32_t>(partSizes.size()), "unknown legacy part type");
        const auto size = partSizes[static_cast<std::size_t>(type)];
        const auto& parts = content.embeddedRow("nr_part", i).at("part");
        require(parts.is_array() && parts.size() >= size, "legacy part has too few frames");
        writer.writeByte(type);
        // Java only reads the constructor's fixed number of frames. Several
        // actual SQL rows contain additional entries, which remain in snapshot.
        for (std::size_t k = 0; k < size; ++k)
            writer.writeShort(integer(parts[k].at("id"))).writeByte(integer(parts[k].at("dx"))).writeByte(integer(parts[k].at("dy")));
    }
    return writer.take();
}

Bytes skillPaintCache(const ContentSnapshot& content) {
    PacketWriter writer;
    writer.writeShort(count(content.table("nr_skill_paint").size(), 65535));
    for (std::size_t i = 0; i < content.table("nr_skill_paint").size(); ++i) {
        const auto& row = content.table("nr_skill_paint")[i];
        writer.writeShort(integer(row.at("skill_id"))).writeShort(integer(row.at("on_mob"))).writeByte(integer(row.at("num_eff")));
        for (const auto* key : {"skill_stand", "skill_fly"}) {
            const auto& paints = content.embeddedRow("nr_skill_paint", i).at(key);
            writer.writeByte(count(paints.size(), 255));
            for (const auto& paint : paints) {
                writer.writeByte(integer(paint.at("status")));
                for (const auto* field : {"effS0Id", "e0dx", "e0dy", "effS1Id", "e1dx", "e1dy", "effS2Id", "e2dx", "e2dy", "arrowId", "adx", "ady"})
                    writer.writeShort(integer(paint.at(field)));
            }
        }
    }
    return writer.take();
}
} // namespace

LegacyCaches LegacyCaches::build(const ContentSnapshot& content, CacheVersions versions) {
    LegacyCaches result;
    result.versions_ = versions;
    const auto groups = heads(content);
    const auto versionSum = static_cast<std::int64_t>(content.table("nr_item_option_template").size()) +
        static_cast<std::int64_t>(content.items().size()) + versions.itemBase + static_cast<std::int64_t>(groups.size());
    result.itemVersion_ = static_cast<std::uint8_t>(versionSum < 0 ? -versionSum : versionSum);
    PacketWriter version;
    version.writeByte(versions.data).writeByte(versions.map).writeByte(versions.skill).writeByte(result.itemVersion_)
        .writeByte(count(content.table("nr_power").size(), 255));
    for (const auto& row : content.table("nr_power")) version.writeLong(longInteger(row.at("power")));
    result.version_ = version.take();
    result.map_ = mapCache(content, versions);
    result.skill_ = skillCache(content, versions);
    const auto order = itemIterationOrder(content);
    for (std::int32_t type = 0; type < 3; ++type)
        result.items_[static_cast<std::size_t>(type)] = itemCache(content, result.itemVersion_, type, order);
    PacketWriter head;
    head.writeByte(result.itemVersion_).writeByte(100).writeShort(count(groups.size(), 65535));
    for (const auto& group : groups) {
        head.writeByte(count(group.size(), 255));
        for (const auto id : group) head.writeShort(id);
    }
    result.heads_ = head.take();
    PacketWriter data;
    data.writeByte(versions.data);
    const std::array<Bytes, 6> animation{{dartCache(content), arrowCache(content), effectCache(content),
                                        imageCache(content), partCache(content), skillPaintCache(content)}};
    for (const auto& bytes : animation) data.writeInt(count(bytes.size(), 0x7fffffffU)).writeBytes(bytes);
    result.data_ = data.take();
    return result;
}

Packet LegacyCaches::versionPacket() const { return notMap(4, version_); }
Packet LegacyCaches::dataPacket() const { return Packet{-87, data_}; }
Packet LegacyCaches::mapPacket() const { return notMap(6, map_); }
Packet LegacyCaches::skillPacket() const { return notMap(7, skill_); }
Packet LegacyCaches::itemPacket(std::int32_t type) const {
    if (type == 100) return notMap(8, heads_);
    if (type < 0 || type >= static_cast<std::int32_t>(items_.size())) throw std::out_of_range("HUNR item cache type must be 0, 1, 2 or 100");
    return notMap(8, items_[static_cast<std::size_t>(type)]);
}

} // namespace game::legacy
