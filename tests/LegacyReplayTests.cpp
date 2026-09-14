#include "game/legacy/LegacyReplay.h"
#include <functional>
#include <limits>
#include <stdexcept>

#ifndef GAME_HUNR_CONTENT_FILE
#define GAME_HUNR_CONTENT_FILE "content/hunr/content.json"
#endif

namespace {
using Json = nlohmann::json;
void replayCheck(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(std::string("LegacyReplayTests: ") + message);
}
void rejectsReplay(const std::function<void()>& operation) {
    try { operation(); } catch (const std::exception&) { return; }
    throw std::runtime_error("LegacyReplayTests: malformed replay accepted");
}
Json state(std::int64_t mana, std::int64_t damage, int x) {
    return {{"hp",1000},{"max_hp",1000},{"mana",mana},{"max_mana",mana},{"damage_full",damage},
            {"critical_full",0},{"stamina",100},{"x",x},{"y",100}};
}
Json fixture() {
    Json draws = Json::array();
    for (int i = 0; i < 2; ++i) {
        draws.push_back({{"min",0},{"max",10},{"value",9}});
        draws.push_back({{"min",0},{"max",100},{"value",99}});
        draws.push_back({{"min",90},{"max",100},{"value",99}});
        draws.push_back({{"min",0},{"max",100},{"value",99}});
    }
    return {
        {"format","hunr-legacy-replay"},{"version",1},{"map_id",0},{"start_time_ms",10000},
        {"actors",Json::array({{{"id",1},{"class_id",0},{"selected_skill_id",0},
            {"learned",Json::array({{{"skill_id",0},{"point",1}},{{"skill_id",19},{"point",1}}})},
            {"state",state(50000,100,100)}}})},
        {"mobs",Json::array({{{"id",1},{"template_id",1},{"state",state(0,0,110)}}})},
        {"random",{{"draws",draws}}},
        {"operations",Json::array({
            {{"op","attack_mob"},{"at_ms",10000},{"actor",1},{"target",1}},
            {{"op","attack_mob"},{"at_ms",10000},{"actor",1},{"target",1}},
            {{"op","attack_mob"},{"at_ms",10500},{"actor",1},{"target",1}}
        })}
    };
}
} // namespace

void runLegacyReplayTests() {
    using namespace game::legacy;
    const auto content = std::make_shared<const ContentSnapshot>(ContentSnapshot::load(GAME_HUNR_CONTENT_FILE));
    const auto input = fixture();
    const auto report = runReplay(content,input);
    replayCheck(report.at("steps")[0].at("damage") == 99 && report.at("steps")[1].at("outcome") == "rejected" &&
                report.at("steps")[1].at("reason") == "cooldown", "real world attack and cooldown route");
    replayCheck(report.at("final").at("mobs")[0].at("hp") == 802 &&
                report.at("final").at("actors")[0].at("state").at("mana") == 49998, "authoritative final snapshots");
    replayCheck(report.at("random").at("draws_used") == 8 && report.at("random").at("unused_draws") == 0,
                "rejected attack consumed no random draws");
    replayCheck(runReplay(content,input) == report, "same scripted replay is deterministic");
    replayCheck(input.at("actors")[0].at("state").at("mana") == 50000, "scenario JSON not mutated");

    auto seeded = input; seeded["random"] = {{"seed",std::uint64_t{123456789}}};
    replayCheck(runReplay(content,seeded) == runReplay(content,seeded), "seeded replay is deterministic");
    auto shield = input;
    shield["operations"] = Json::array({
        {{"op","select_skill"},{"at_ms",10000},{"actor",1},{"skill_id",19}},
        {{"op","nonfocus"},{"at_ms",10000},{"actor",1},{"action_type",9}},
        {{"op","advance"},{"at_ms",25000}}
    });
    const auto shieldReport = runReplay(content,shield);
    replayCheck(shieldReport.at("steps")[1].at("outcome") == "applied" &&
                shieldReport.at("final").at("actors")[0].at("state").at("mana") == 24500 &&
                shieldReport.at("final").at("actors")[0].at("state").at("protected_by_energy") == false &&
                shieldReport.at("final").at("effects").empty(), "real imported shield cost and scheduled expiry");
    auto movement = input;
    movement["operations"] = Json::array({{{"op","move"},{"at_ms",10000},{"actor",1},{"x",120},{"y",100},{"flight",false}}});
    const auto unavailable = runReplay(content,movement);
    replayCheck(unavailable.at("steps")[0].at("outcome") == "unsupported" &&
                unavailable.at("final").at("actors")[0].at("state").at("x") == 100, "no fabricated terrain movement");

    auto badDraw = input; badDraw["random"]["draws"][0]["max"] = 11;
    const auto failedDraw = runReplay(content,badDraw);
    replayCheck(failedDraw.at("steps")[0].at("outcome") == "unsupported" &&
                failedDraw.at("final").at("actors")[0].at("state").at("mana") == 50000, "random domain mismatch does not mutate live state");
    auto malformed = input; malformed["unknown"] = 1;
    rejectsReplay([&] { runReplay(content,malformed); });
    malformed = input; malformed["actors"][0]["state"]["hp"] = std::numeric_limits<double>::infinity();
    rejectsReplay([&] { runReplay(content,malformed); });
    malformed = input; malformed["actors"][0]["state"]["hp"] = (std::numeric_limits<std::uint64_t>::max)();
    rejectsReplay([&] { runReplay(content,malformed); });
    malformed = input; malformed["actors"][0]["learned"].push_back({{"skill_id",0},{"point",1}});
    rejectsReplay([&] { runReplay(content,malformed); });
    malformed = input; malformed["random"]["seed"] = 0;
    rejectsReplay([&] { runReplay(content,malformed); });
    malformed = input; malformed["operations"][0]["op"] = "execute_code";
    rejectsReplay([&] { runReplay(content,malformed); });
    malformed = input; malformed["operations"][0]["at_ms"] = 1; malformed["operations"][0].erase("actor");
    rejectsReplay([&] { runReplay(content,malformed); });
}
