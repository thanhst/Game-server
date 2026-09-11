#include "game/legacy/Identity.h"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace game::legacy {
std::string normalizeUsername(std::string username) {
    for (char& ch : username) if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    return username;
}
bool isLegacyUsername(const std::string& username) noexcept {
    if (username.compare(0, 14, "@guest.ingame_") == 0) return true;
    return std::all_of(username.begin(), username.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '@' || c == '_';
    });
}
CreateCharacterStatus validateCharacterName(const std::string& name) noexcept {
    // Accepted names are ASCII, so Java UTF-16 length equals byte length here.
    if (name.size() < 5 || name.size() > 15) return CreateCharacterStatus::InvalidLength;
    if (name.find("admin") != std::string::npos || name.find("server") != std::string::npos)
        return CreateCharacterStatus::ReservedName;
    if (!std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    })) return CreateCharacterStatus::InvalidName;
    return CreateCharacterStatus::Created;
}
CharacterRecord makeNewCharacter(const ContentSnapshot& content, std::int64_t accountId,
    std::int32_t serverId, std::string name, std::int32_t gender,
    std::int32_t hair, std::int64_t nowMs) {
    if (validateCharacterName(name) != CreateCharacterStatus::Created)
        throw std::invalid_argument("invalid HUNR character name");
    if (accountId <= 0 || nowMs < 0) throw std::invalid_argument("invalid character owner/time");
    using Json = nlohmann::json;
    constexpr std::int32_t heads[3][3] = {{64,30,31},{9,29,32},{6,27,28}};
    gender = gender < 0 || gender > 2 ? 0 : gender;
    if (hair != heads[gender][1] && hair != heads[gender][2]) hair = heads[gender][0];
    auto item = [&](int id, int index, int quantity) {
        return Json{{"id",id},{"index",index},{"quantity",quantity},
                    {"options",content.items().at(id).options}};
    };
    Json body = Json::array({item(gender,0,1),item(6+gender,1,1)});
    auto goldBars = item(457,1,10);
    goldBars["options"].push_back(Json{{"id",30},{"param",0}});
    Json box = Json::array({item(12,0,1),goldBars,item(595,2,999999),item(454,3,1)});
    const auto hp = gender == 0 ? 200 : 100;
    const auto mp = gender == 1 ? 200 : 100;
    Json info{{"hp_goc",hp},{"mp_goc",mp},{"damage",gender == 2 ? 15 : 12},
        {"defense",0},{"critical",0},{"power",1500000},{"potential",1500000},
        {"stamina",10000},{"max_stamina",10000},{"hp",hp},{"mp",mp},
        {"open_power",0},{"active_point",0}};
    Json achievements = Json::array();
    for (std::size_t i = 0; i < content.table("nr_achievement").size(); ++i)
        achievements.push_back(Json{{"id",i},{"count",0},{"rewarded",false}});
    CharacterRecord result;
    result.accountId = accountId;
    result.name = std::move(name);
    result.data = Json{{"user_id",accountId},{"server_id",serverId},{"name",result.name},
        {"gender",gender},{"class_id",gender},{"head",hair},
        {"task",{{"id",0},{"index",2},{"count",0},{"lastTask",0}}},
        {"gold",10000000000LL},{"gem",500000},{"gem_lock",0},
        {"item_bag",Json::array({item(194,0,1)})},{"item_body",body},{"item_box",box},
        {"box_crack_ball",Json::array()},{"map",Json::array({21+gender,100,336})},
        {"skill",Json::array()},{"info",info},{"clan",-1},
        {"shortcut",Json::array({-1,-1,-1,-1,-1,-1,-1,-1,-1,-1})},
        {"magic_tree",{{"level",10},{"upgrade",false},{"upgrade_time",0},{"last_harvest",0}}},
        {"number_cell_bag",100},{"number_cell_box",100},{"friend",Json::array()},
        {"enemy",Json::array()},{"ship",0},{"fusion",1},{"porata",0},
        {"item_time",Json::array()},{"amulet",Json::array()},{"achievement",achievements},
        {"time_played",0},{"type_trainning",0},{"online",0},{"time_at_split_fusion",0},
        {"head2",-1},{"body",-1},{"leg",-1},{"collection_book",Json::array()},
        {"dataDHVT23",Json::array({0,0,0,0,0,0,0,0,0,0,0,0,0})},{"thoivang",0},
        {"drop_item",Json::object()},{"count_number_of_specialskill_changes",0},
        {"create_time",nowMs},{"reset_time",nowMs}};
    return result;
}
} // namespace game::legacy
