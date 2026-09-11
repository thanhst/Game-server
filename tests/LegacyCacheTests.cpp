#include "game/legacy/LegacyCaches.h"

#include <array>
#include <stdexcept>
#include <string>

#ifndef GAME_HUNR_CONTENT_FILE
#define GAME_HUNR_CONTENT_FILE "content/hunr/content.json"
#endif

namespace {
void checkCache(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(std::string("LegacyCacheTests: ") + message);
}
} // namespace

void runLegacyCacheTests() {
    using namespace game::legacy;
    const auto content = ContentSnapshot::load(GAME_HUNR_CONTENT_FILE);
    const auto caches = LegacyCaches::build(content, CacheVersions{11, 12, 13, 14});
    // Java (byte)abs(248 option templates + 1579 item templates + 14 + 7 head groups).
    checkCache(caches.effectiveItemVersion() == 56, "effective Java item version");
    const auto versionPacket = caches.versionPacket();
    checkCache(versionPacket.command == -28, "version NOT_MAP envelope");
    PacketReader version(versionPacket.payload);
    checkCache(version.readByte() == 4 && version.readByte() == 11 && version.readByte() == 12 &&
               version.readByte() == 13 && version.readUnsignedByte() == 56 && version.readByte() == 22,
               "version subcommand and byte field order");
    for (const auto& power : content.table("nr_power"))
        checkCache(version.readLong() == power.at("power").get<std::int64_t>(), "full int64 power progression values");
    version.requireEnd();

    const auto skillPacket = caches.skillPacket();
    checkCache(skillPacket.command == -28, "skill NOT_MAP envelope");
    PacketReader skill(skillPacket.payload);
    checkCache(skill.readByte() == 7 && skill.readByte() == 13 && skill.readByte() == 63,
               "skill subcommand/version/options count");
    for (int i = 0; i < 63; ++i) skill.readModifiedUTF();
    checkCache(skill.readByte() == 7, "all seven Java classes advertised");
    constexpr std::array<int, 7> classSizes{{8, 8, 8, 1, 2, 1, 1}};
    int totalLevels = 0;
    for (int classId = 0; classId < 7; ++classId) {
        const auto className = skill.readModifiedUTF();
        if (classId == 0) checkCache(className == u"Trái đất", "modified UTF Vietnamese class name");
        checkCache(skill.readUnsignedByte() == classSizes[static_cast<std::size_t>(classId)], "source class skill count");
        for (const auto& row : content.table("nr_skill")) {
            if (row.at("class") != classId) continue;
            const auto& definition = content.skill(classId, row.at("skill_id").get<int>());
            checkCache(skill.readUnsignedByte() == static_cast<int>(definition.id), "SQL template ordering within class");
            skill.readModifiedUTF();
            checkCache(skill.readUnsignedByte() == definition.maxPoint && skill.readUnsignedByte() == definition.manaUseType &&
                       skill.readUnsignedByte() == definition.type && skill.readShort() == definition.icon,
                       "skill template numeric fields");
            skill.readModifiedUTF();
            skill.readModifiedUTF();
            checkCache(skill.readUnsignedByte() == definition.levels.size(), "skill source level count");
            for (const auto& level : definition.levels) {
                checkCache(skill.readShort() == level.id && skill.readUnsignedByte() == level.point &&
                           skill.readLong() == level.powerRequire && skill.readShort() == level.manaUse &&
                           skill.readInt() == level.cooldownMs && skill.readShort() == level.dx && skill.readShort() == level.dy &&
                           skill.readUnsignedByte() == level.maxFight && skill.readShort() == level.damage && skill.readShort() == level.price,
                           "legacy skill level binary layout");
                skill.readModifiedUTF();
                ++totalLevels;
            }
        }
    }
    checkCache(totalLevels == 191, "all real skill levels serialized");
    skill.requireEnd();

    const auto mapPacket = caches.mapPacket();
    PacketReader map(mapPacket.payload);
    checkCache(mapPacket.command == -28 && map.readByte() == 6 && map.readByte() == 12 && map.readUnsignedShort() == 185,
               "map cache envelope and map-name count");
    for (int i = 0; i < 185; ++i) map.readModifiedUTF();
    checkCache(map.readUnsignedByte() == 76, "NPC template count");
    for (int i = 0; i < 76; ++i) {
        map.readModifiedUTF();
        map.skip(6);
        const auto menus = map.readUnsignedByte();
        for (int menu = 0; menu < menus; ++menu) {
            const auto labels = map.readUnsignedByte();
            for (int label = 0; label < labels; ++label) map.readModifiedUTF();
        }
    }
    checkCache(map.readUnsignedByte() == 82, "mob template count");
    for (int id = 0; id < 82; ++id) {
        checkCache(map.readUnsignedByte() == id, "mob source order");
        map.readByte();
        map.readModifiedUTF();
        checkCache(map.readLong() == content.mobs().at(id).hp, "mob hp encoded as Java long");
        map.skip(3);
    }
    map.requireEnd();

    std::size_t itemIndex = 0;
    auto orderedItem = content.items().begin();
    for (int type : {1, 2}) {
        const auto packet = caches.itemPacket(type);
        PacketReader item(packet.payload);
        checkCache(packet.command == -28 && item.readByte() == 8 && item.readUnsignedByte() == 56 && item.readByte() == type,
                   "item cache envelope");
        checkCache(item.readUnsignedShort() == 800, "Java item cache split");
        if (type == 2) checkCache(item.readUnsignedShort() == 1579, "item total count in second cache");
        const auto end = type == 1 ? 800U : 1579U;
        for (; itemIndex < end; ++itemIndex, ++orderedItem) {
            checkCache(item.readShort() == orderedItem->first, "actual no-collision Java HashMap item iteration order");
            item.skip(2);
            item.readModifiedUTF();
            item.readModifiedUTF();
            item.skip(1 + 4 + 2 + 2 + 1);
        }
        item.requireEnd();
    }
    checkCache(itemIndex == 1579, "no dropped/duplicated items across split");
    const auto headPacket = caches.itemPacket(100);
    PacketReader head(headPacket.payload);
    checkCache(head.readByte() == 8 && head.readUnsignedByte() == 56 && head.readByte() == 100 && head.readUnsignedShort() == 7,
               "head group cache layout");
    checkCache(head.readByte() == 3 && head.readShort() == 1324 && head.readShort() == 1345 && head.readShort() == 1346,
               "first actual head group");
    for (int group = 1; group < 7; ++group) head.skip(static_cast<std::size_t>(head.readUnsignedByte()) * 2);
    head.requireEnd();

    const auto dataPacket = caches.dataPacket();
    checkCache(dataPacket.command == -87, "UPDATE_DATA top-level command");
    PacketReader data(dataPacket.payload);
    checkCache(data.readByte() == 11, "animation data version");
    constexpr std::array<int, 6> paintCounts{{64, 1, 173, 2997, 2204, 157}};
    for (const auto expected : paintCounts) {
        const auto size = data.readInt();
        checkCache(size >= 2, "animation cache byte length");
        const auto blob = data.readBytes(static_cast<std::size_t>(size));
        PacketReader animation(blob);
        checkCache(animation.readUnsignedShort() == expected, "dart/arrow/effect/image/part/skill-paint cache ordering");
    }
    data.requireEnd();
    bool rejected = false;
    try { caches.itemPacket(3); }
    catch (const std::out_of_range&) { rejected = true; }
    checkCache(rejected, "unknown item cache type rejected");
}
