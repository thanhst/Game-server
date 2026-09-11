#include "game/legacy/LegacyContent.h"

#include <functional>
#include <limits>
#include <stdexcept>
#include <string>

#ifndef GAME_HUNR_CONTENT_FILE
#define GAME_HUNR_CONTENT_FILE "content/hunr/content.json"
#endif

namespace {
using Json = nlohmann::json;
using game::legacy::ContentSnapshot;

void checkContent(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(std::string("LegacyContentTests: ") + message);
}

void rejectsContent(const std::function<void()>& operation) {
    try { operation(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error("LegacyContentTests: invalid content was accepted");
}

Json skillFixture(const ContentSnapshot& source) {
    Json fixture{{"format", "hunr-source-content"}, {"version", 1},
                 {"schema", Json::object()}, {"tables", Json::object()}};
    for (const auto* table : {"nr_skill", "nr_item", "nr_item_option_template", "nr_mob_template", "nr_npc_template", "nr_map"}) {
        fixture["schema"][table] = source.document().at("schema").at(table);
        fixture["tables"][table] = {{"rows", Json::array()}, {"embedded_json", Json::array()}};
    }
    for (std::size_t i = 0; i < source.table("nr_skill").size(); ++i) {
        const auto& row = source.table("nr_skill")[i];
        if ((row.at("class") == 0 && row.at("skill_id") == 0) ||
            (row.at("skill_id") == 19 && (row.at("class") == 0 || row.at("class") == 1))) {
            fixture["tables"]["nr_skill"]["rows"].push_back(row);
            fixture["tables"]["nr_skill"]["embedded_json"].push_back(source.embeddedRow("nr_skill", i));
        }
    }
    return fixture;
}
} // namespace

void runLegacyContentTests() {
    const auto full = ContentSnapshot::load(GAME_HUNR_CONTENT_FILE);
    checkContent(full.rowCount() == 9302, "retained SQL row count");
    checkContent(full.document().at("tables").size() == 52, "all static tables imported");
    checkContent(full.document().at("schema").size() == 102, "all schema definitions retained");
    checkContent(full.skills().size() == 29 && full.skillLevelCount() == 191, "real class skill and level counts");
    checkContent(full.items().size() == 1579 && full.maps().size() == 185 &&
                 full.mobs().size() == 82 && full.npcs().size() == 76, "typed content count");
    checkContent(full.skill(0, 0).level(1).powerRequire == 1000 && full.skill(0, 0).level(1).manaUse == 1 &&
                 full.skill(0, 0).level(1).cooldownMs == 500, "Dragon first level matches SQL exactly");
    checkContent(full.skill(0, 9).levels.front().point == 2 && full.skill(0, 9).levels.back().point == 1,
                 "Kaioken source level array order is preserved");
    checkContent(full.skill(4, 15).level(0).id == 105 && full.skill(4, 16).level(0).id == 106,
                 "Java class 4 point-zero skills are loaded");
    checkContent(full.skillLevel(0, 121).id == full.skillLevel(1, 121).id &&
                 &full.skillLevel(0, 121) != &full.skillLevel(1, 121), "shield levels remain class scoped");
    checkContent(full.maps().at(171).mobs.size() == 14 && full.maps().at(174).mobs.size() == 19 &&
                 full.maps().at(176).mobs.size() == 14, "Java trailing comma spawn arrays retain all entries");
    checkContent(full.items().at(9).options.at(0).at("id").is_number_integer(),
                 "typed item option follows JSONObject.getInt numeric string conversion");
    checkContent(full.table("nr_item").at(9).at("options").is_string(), "raw SQL options remain a string");

    const auto fixture = skillFixture(full);
    const auto tiny = ContentSnapshot::fromJson(fixture);
    checkContent(tiny.skills().size() == 3, "fixture preserves same skill ID for different classes");
    checkContent(tiny.skillLevel(0, 121).point == 1 && tiny.skillLevel(1, 121).point == 1,
                 "level row lookup includes class ID");
    auto copy = tiny;
    checkContent(copy.skillLevel(0, 121).point == 1, "copied registry resolves indices without dangling pointers");

    auto changed = fixture;
    changed["tables"]["nr_skill"]["embedded_json"][0]["skills"][0]["power_require"] = std::uint64_t{9007199254740993ULL};
    checkContent(ContentSnapshot::fromJson(changed).skill(0, 0).level(1).powerRequire == 9007199254740993LL,
                 "int64 power above double exact range is preserved");
    changed = fixture;
    changed["tables"]["nr_skill"]["embedded_json"][0]["skills"][0]["damage"] = 65535;
    checkContent(ContentSnapshot::fromJson(changed).skill(0, 0).level(1).damage == -1,
                 "explicit Java short narrowing is preserved");
    changed = fixture;
    changed["tables"]["nr_skill"]["embedded_json"][0]["skills"][0]["max_fight"] = std::uint64_t{4294967296ULL};
    rejectsContent([&] { ContentSnapshot::fromJson(changed); });
    changed = fixture;
    changed["tables"]["nr_skill"]["embedded_json"][0]["skills"][0]["power_require"] =
        (std::numeric_limits<std::uint64_t>::max)();
    rejectsContent([&] { ContentSnapshot::fromJson(changed); });
    changed = fixture;
    changed["tables"]["nr_skill"]["embedded_json"][0]["skills"][0]["mana_use"] = 1.5;
    rejectsContent([&] { ContentSnapshot::fromJson(changed); });
    changed = fixture;
    auto duplicate = changed["tables"]["nr_skill"]["rows"][0];
    duplicate["id"] = 9999;
    changed["tables"]["nr_skill"]["rows"].push_back(duplicate);
    const auto duplicateEmbedded = changed["tables"]["nr_skill"]["embedded_json"][0];
    changed["tables"]["nr_skill"]["embedded_json"].push_back(duplicateEmbedded);
    rejectsContent([&] { ContentSnapshot::fromJson(changed); });
    changed = fixture;
    changed["tables"]["nr_skill"]["embedded_json"].erase(0);
    rejectsContent([&] { ContentSnapshot::fromJson(changed); });
    changed = fixture;
    changed["tables"]["nr_skill"]["rows"][0].erase("name");
    rejectsContent([&] { ContentSnapshot::fromJson(changed); });
}
