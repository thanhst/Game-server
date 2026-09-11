#include "game/legacy/LegacyContent.h"

#include <fstream>
#include <charconv>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace game::legacy {
namespace {
using Json = nlohmann::json;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::invalid_argument("HUNR content: " + message);
}

std::int64_t number(const Json& value, std::int64_t minimum, std::int64_t maximum,
                    const std::string& label) {
    require(value.is_number_integer(), label + " must be an integer");
    if (value.is_number_unsigned()) {
        const auto unsignedValue = value.get<std::uint64_t>();
        require(unsignedValue <= static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()),
                label + " exceeds int64");
        const auto result = static_cast<std::int64_t>(unsignedValue);
        require(result >= minimum && result <= maximum, label + " is out of range");
        return result;
    }
    const auto result = value.get<std::int64_t>();
    require(result >= minimum && result <= maximum, label + " is out of range");
    return result;
}

std::int32_t intField(const Json& object, const char* field) {
    return static_cast<std::int32_t>(number(object.at(field), (std::numeric_limits<std::int32_t>::min)(),
                                           (std::numeric_limits<std::int32_t>::max)(), field));
}

std::int64_t longField(const Json& object, const char* field) {
    return number(object.at(field), (std::numeric_limits<std::int64_t>::min)(),
                  (std::numeric_limits<std::int64_t>::max)(), field);
}

// JSONObject.getInt also accepts a decimal integer string. The SQL actually
// contains 594 item options with quoted id/param, alongside numeric options.
std::int32_t javaIntField(const Json& object, const char* field) {
    const auto& value = object.at(field);
    if (!value.is_string()) return intField(object, field);
    const auto source = value.get<std::string>();
    const auto* begin = source.data();
    const auto* end = begin + source.size();
    if (begin != end && *begin == '+') ++begin;
    std::int32_t parsed = 0;
    const auto result = std::from_chars(begin, end, parsed);
    require(result.ec == std::errc{} && result.ptr == end && begin != end,
            std::string(field) + " must contain a Java int");
    return parsed;
}

std::string textField(const Json& object, const char* field) {
    const auto& value = object.at(field);
    require(value.is_string(), std::string(field) + " must be a string");
    return value.get<std::string>();
}

// Java explicit (short) narrowing is modulo 65536. Avoid implementation-defined
// conversion of a C++ out-of-range signed integer.
std::int16_t javaShort(std::int32_t value) noexcept {
    const auto bits = static_cast<std::uint32_t>(value) & 0xffffU;
    return static_cast<std::int16_t>(bits >= 0x8000U ? static_cast<std::int32_t>(bits) - 65536
                                                   : static_cast<std::int32_t>(bits));
}

const Json& arrayField(const Json& row, const Json& embedded, const char* field) {
    static const Json empty = Json::array();
    if (row.at(field).is_null()) return empty;
    require(row.at(field).is_string(), std::string(field) + " SQL value must be string/null");
    require(embedded.contains(field) && embedded.at(field).is_array(),
            std::string(field) + " requires a parsed embedded JSON array");
    return embedded.at(field);
}

template<class Registry, class Definition>
void insertUnique(Registry& registry, Definition definition, const char* kind) {
    const auto id = definition.id;
    require(registry.emplace(id, std::move(definition)).second,
            std::string("duplicate ") + kind + " ID " + std::to_string(id));
}
} // namespace

ContentSnapshot ContentSnapshot::load(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open HUNR content: " + file.string());
    try {
        auto document = Json::parse(input);
        return fromJson(std::move(document));
    } catch (const std::exception& error) {
        throw std::runtime_error("Cannot load HUNR content " + file.string() + ": " + error.what());
    }
}

ContentSnapshot ContentSnapshot::fromJson(Json document) {
    ContentSnapshot result;
    result.document_ = std::move(document);
    result.validateAndIndex();
    return result;
}

const Json& ContentSnapshot::table(std::string_view name) const {
    return document_.at("tables").at(std::string(name)).at("rows");
}

const Json& ContentSnapshot::embeddedRow(std::string_view name, std::size_t rowIndex) const {
    return document_.at("tables").at(std::string(name)).at("embedded_json").at(rowIndex);
}

const SkillTemplate& ContentSnapshot::skill(std::int32_t classId, std::int32_t skillId) const {
    return skills_.at(SkillKey{classId, skillId});
}

const SkillLevel& ContentSnapshot::skillLevel(std::int32_t classId, std::int32_t levelRowId) const {
    const auto& location = skillLevels_.at({classId, levelRowId});
    return skills_.at(location.first).levels.at(location.second);
}

void ContentSnapshot::validateAndIndex() {
    require(document_.is_object(), "snapshot root must be an object");
    require(document_.at("format") == "hunr-source-content" && document_.at("version") == 1,
            "unsupported snapshot format/version");
    const auto& tables = document_.at("tables");
    const auto& schemas = document_.at("schema");
    require(tables.is_object() && schemas.is_object(), "tables and schema must be objects");
    for (auto tableIt = tables.begin(); tableIt != tables.end(); ++tableIt) {
        const auto& name = tableIt.key();
        const auto& rows = tableIt.value().at("rows");
        const auto& embedded = tableIt.value().at("embedded_json");
        require(rows.is_array() && embedded.is_array() && rows.size() == embedded.size(),
                name + " row/embedded array lengths differ");
        const auto& schema = schemas.at(name);
        const auto& columns = schema.at("columns");
        const auto& primaryKey = schema.at("primary_key");
        require(columns.is_array() && !columns.empty() && primaryKey.is_array(), name + " invalid schema");
        std::set<std::string> columnNames, primaryKeys;
        for (const auto& column : columns) {
            require(columnNames.insert(textField(column, "name")).second, name + " duplicate schema column");
        }
        for (std::size_t index = 0; index < rows.size(); ++index) {
            const auto& row = rows[index];
            require(row.is_object() && row.size() == columns.size(), name + " row columns differ from schema");
            require(embedded[index].is_object(), name + " embedded row must be object");
            for (const auto& column : columns) {
                const auto field = textField(column, "name");
                require(row.contains(field), name + " missing column " + field);
                require(column.at("nullable").is_boolean(), name + " nullable must be boolean");
                require(!row[field].is_null() || column.at("nullable").get<bool>(), name + "." + field + " cannot be null");
            }
            for (auto fieldIt = embedded[index].begin(); fieldIt != embedded[index].end(); ++fieldIt) {
                require(row.contains(fieldIt.key()) && row.at(fieldIt.key()).is_string(),
                        name + " embedded field must have SQL string source");
                require(fieldIt.value().is_array() || fieldIt.value().is_object(), name + " embedded field is not a JSON container");
            }
            Json key = Json::array();
            for (const auto& field : primaryKey) key.push_back(row.at(field.get<std::string>()));
            require(primaryKey.empty() || primaryKeys.insert(key.dump()).second, name + " duplicate primary key");
        }
        rowCount_ += rows.size();
    }

    for (std::size_t index = 0; index < table("nr_skill").size(); ++index) {
        const auto& row = table("nr_skill")[index];
        const auto& levels = arrayField(row, embeddedRow("nr_skill", index), "skills");
        SkillTemplate definition;
        definition.id = static_cast<SkillId>(intField(row, "skill_id"));
        definition.classId = intField(row, "class");
        definition.name = textField(row, "name");
        definition.maxPoint = intField(row, "max_point");
        definition.manaUseType = intField(row, "mana_use_type");
        definition.type = intField(row, "type");
        definition.icon = intField(row, "icon");
        definition.description = textField(row, "description");
        definition.damageInfo = textField(row, "info");
        const SkillKey key{definition.classId, static_cast<std::int32_t>(definition.id)};
        for (const auto& source : levels) {
            SkillLevel level;
            level.id = intField(source, "id");
            level.point = intField(source, "point");
            level.powerRequire = longField(source, "power_require");
            level.cooldownMs = intField(source, "cool_down");
            level.dx = intField(source, "dx");
            level.dy = intField(source, "dy");
            level.maxFight = intField(source, "max_fight");
            level.manaUse = intField(source, "mana_use");
            level.damage = javaShort(intField(source, "damage"));
            level.price = javaShort(intField(source, "price"));
            level.moreInfo = textField(source, "more_info");
            require(skillLevels_.emplace(std::make_pair(definition.classId, level.id),
                                        std::make_pair(key, definition.levels.size())).second,
                    "duplicate skill level row ID within one class");
            definition.levels.push_back(std::move(level));
        }
        definition.validate();
        skillLevelCount_ += definition.levels.size();
        require(skills_.emplace(key, std::move(definition)).second, "duplicate class/skill template key");
    }

    for (std::size_t index = 0; index < table("nr_item").size(); ++index) {
        const auto& row = table("nr_item")[index];
        ItemTemplate definition;
        definition.id = intField(row, "id");
        definition.name = textField(row, "name");
        definition.description = textField(row, "description");
        definition.type = intField(row, "type");
        definition.gender = intField(row, "gender");
        definition.level = intField(row, "level");
        definition.powerRequire = longField(row, "require");
        definition.resalePrice = intField(row, "resale_price");
        definition.icon = intField(row, "icon");
        definition.part = intField(row, "part");
        definition.head = intField(row, "head");
        definition.body = intField(row, "body");
        definition.leg = intField(row, "leg");
        definition.mountId = intField(row, "mount_id");
        definition.upToUp = intField(row, "is_up_to_up") != 0;
        definition.locked = intField(row, "lock") != 0;
        definition.options = arrayField(row, embeddedRow("nr_item", index), "options");
        for (auto& option : definition.options) {
            option["id"] = javaIntField(option, "id");
            option["param"] = javaIntField(option, "param");
        }
        insertUnique(items_, std::move(definition), "item");
    }

    for (const auto& row : table("nr_mob_template")) {
        MobTemplate definition;
        definition.id = intField(row, "id");
        definition.name = textField(row, "name");
        definition.level = intField(row, "level");
        definition.type = intField(row, "type");
        definition.newType = intField(row, "new");
        definition.hp = longField(row, "hp");
        definition.rangeMove = intField(row, "range_move");
        definition.speed = intField(row, "speed");
        definition.dartType = intField(row, "dart_type");
        insertUnique(mobs_, std::move(definition), "mob");
    }

    for (std::size_t index = 0; index < table("nr_npc_template").size(); ++index) {
        const auto& row = table("nr_npc_template")[index];
        NpcTemplate definition;
        definition.id = intField(row, "id");
        definition.name = textField(row, "name");
        definition.head = intField(row, "head");
        definition.body = intField(row, "body");
        definition.leg = intField(row, "leg");
        definition.menu = arrayField(row, embeddedRow("nr_npc_template", index), "menu");
        insertUnique(npcs_, std::move(definition), "NPC");
    }

    for (std::size_t index = 0; index < table("nr_map").size(); ++index) {
        const auto& row = table("nr_map")[index];
        const auto& embedded = embeddedRow("nr_map", index);
        MapTemplate definition;
        definition.id = intField(row, "id");
        definition.name = textField(row, "name");
        definition.planet = intField(row, "planet");
        definition.tileId = intField(row, "tile_id");
        definition.backgroundId = intField(row, "bg_id");
        definition.type = intField(row, "type");
        definition.backgroundType = intField(row, "bg_type");
        definition.zoneCount = intField(row, "zone_number");
        definition.waypoints = arrayField(row, embedded, "waypoint");
        definition.mobs = arrayField(row, embedded, "mob");
        definition.npcs = arrayField(row, embedded, "npc");
        definition.backgroundItems = arrayField(row, embedded, "position_bg_item");
        definition.effects = arrayField(row, embedded, "effect");
        definition.eventEffects = arrayField(row, embedded, "effect_event");
        require(definition.zoneCount >= 0, "map zone count cannot be negative");
        insertUnique(maps_, std::move(definition), "map");
    }

    // These are actual Java lookup dependencies. Do not invent foreign keys for
    // sentinel -1 values, client image metadata or hardcoded boss classes.
    for (const auto& entry : items_) {
        for (const auto& option : entry.second.options) {
            const auto id = intField(option, "id");
            bool found = false;
            for (const auto& optionTemplate : table("nr_item_option_template"))
                if (intField(optionTemplate, "id") == id) { found = true; break; }
            require(found, "item " + std::to_string(entry.first) + " has unknown option " + std::to_string(id));
            intField(option, "param");
        }
    }
    for (const auto& entry : maps_) {
        for (const auto& mob : entry.second.mobs) {
            const auto id = intField(mob, "id");
            require(mobs_.count(id) != 0, "map " + std::to_string(entry.first) + " has unknown mob " + std::to_string(id));
            intField(mob, "x");
            intField(mob, "y");
            if (mob.contains("hp")) longField(mob, "hp");
        }
        for (const auto& npc : entry.second.npcs) {
            const auto id = intField(npc, "id");
            require(npcs_.count(id) != 0, "map " + std::to_string(entry.first) + " has unknown NPC " + std::to_string(id));
            intField(npc, "x");
            intField(npc, "y");
        }
        for (const auto& waypoint : entry.second.waypoints) {
            const auto next = intField(waypoint, "next");
            require(maps_.count(next) != 0, "map " + std::to_string(entry.first) + " has unknown destination " + std::to_string(next));
        }
    }

    if (document_.contains("counts")) {
        const auto& counts = document_.at("counts");
        require(number(counts.at("rows"), 0, (std::numeric_limits<std::int64_t>::max)(), "row count") ==
                    static_cast<std::int64_t>(rowCount_), "row count metadata mismatch");
        require(number(counts.at("skill_templates"), 0, (std::numeric_limits<std::int64_t>::max)(), "skill count") ==
                    static_cast<std::int64_t>(skills_.size()), "skill count metadata mismatch");
        require(number(counts.at("skill_levels"), 0, (std::numeric_limits<std::int64_t>::max)(), "level count") ==
                    static_cast<std::int64_t>(skillLevelCount_), "level count metadata mismatch");
    }
}

} // namespace game::legacy
