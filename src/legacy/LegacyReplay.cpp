#include "game/legacy/LegacyReplay.h"
#include "game/legacy/LegacyWorld.h"

#include <limits>
#include <random>
#include <set>
#include <stdexcept>

namespace game::legacy {
namespace {
using Json = nlohmann::json;
using I = std::int64_t;
constexpr std::size_t MaxSteps = 10000, MaxDraws = 100000, MaxEntities = 4096;

[[noreturn]] void invalid(const std::string& reason) { throw std::invalid_argument("Legacy replay: " + reason); }
void require(bool condition, const std::string& reason) { if (!condition) invalid(reason); }
I integer(const Json& value, const std::string& label,
          I low = (std::numeric_limits<I>::min)(), I high = (std::numeric_limits<I>::max)()) {
    require(value.is_number_integer(), label + " must be an integer");
    if (value.is_number_unsigned())
        require(value.get<std::uint64_t>() <= static_cast<std::uint64_t>((std::numeric_limits<I>::max)()), label + " exceeds int64");
    const I result = value.get<I>();
    require(result >= low && result <= high, label + " is out of range");
    return result;
}
int int32(const Json& value, const std::string& label) {
    return static_cast<int>(integer(value, label, (std::numeric_limits<std::int32_t>::min)(), (std::numeric_limits<std::int32_t>::max)()));
}
std::string string(const Json& value, const std::string& label) {
    require(value.is_string(), label + " must be a string");
    auto result = value.get<std::string>();
    require(!result.empty() && result.size() <= 4096, label + " must contain 1..4096 bytes");
    return result;
}
bool boolean(const Json& value, const std::string& label) {
    require(value.is_boolean(), label + " must be a boolean"); return value.get<bool>();
}
void keys(const Json& object, std::initializer_list<const char*> allowed, const std::string& label) {
    require(object.is_object(), label + " must be an object");
    for (auto it = object.begin(); it != object.end(); ++it) {
        bool known = false;
        for (const auto* name : allowed) if (it.key() == name) { known = true; break; }
        require(known, label + ": unknown key '" + it.key() + "'");
    }
}
const Json& array(const Json& object, const char* name, std::size_t max) {
    const auto& value = object.at(name);
    require(value.is_array() && value.size() <= max, std::string(name) + " must be a bounded array");
    return value;
}

const std::map<std::string, I BattleActor::*> ActorLongFields{
    {"hp", &BattleActor::hp}, {"max_hp", &BattleActor::maxHp}, {"mana", &BattleActor::mana},
    {"max_mana", &BattleActor::maxMana}, {"damage_full", &BattleActor::damageFull},
    {"option_kame", &BattleActor::optionKame}, {"option_laze", &BattleActor::optionLaze},
    {"last_attack_ms", &BattleActor::lastAttackMs}, {"last_x_chuong_ms", &BattleActor::lastXChuongMs},
    {"last_recovery_ms", &BattleActor::lastRecoveryMs}, {"last_recovery_started_ms", &BattleActor::lastRecoveryStartedMs}
};
const std::map<std::string, std::int32_t BattleActor::*> ActorIntFields{
    {"critical_full", &BattleActor::criticalFull}, {"stamina", &BattleActor::stamina},
    {"special_id", &BattleActor::specialId}, {"special_param", &BattleActor::specialParam},
    {"percent_damage_bonus", &BattleActor::percentDamageBonus}, {"map_phu_ho", &BattleActor::mapPhuHo},
    {"flag", &BattleActor::flag}, {"satellites_343", &BattleActor::satellites343}
};
const std::map<std::string, bool BattleActor::*> ActorBoolFields{
    {"dead", &BattleActor::dead}, {"frozen", &BattleActor::frozen}, {"sleeping", &BattleActor::sleeping},
    {"held", &BattleActor::held}, {"stone", &BattleActor::stone}, {"blind", &BattleActor::blind},
    {"human", &BattleActor::human}, {"boss", &BattleActor::boss}, {"disciple", &BattleActor::disciple},
    {"disciple_type_2", &BattleActor::discipleType2}, {"crit_first_hit", &BattleActor::critFirstHit},
    {"skill_special", &BattleActor::skillSpecial}, {"recovering", &BattleActor::recovering},
    {"charging", &BattleActor::charging}, {"monkey", &BattleActor::monkey},
    {"immortal_charm", &BattleActor::immortalCharm}, {"powerful_charm", &BattleActor::powerfulCharm},
    {"endurance_charm", &BattleActor::enduranceCharm}, {"heroic_charm", &BattleActor::heroicCharm},
    {"disciple_charm", &BattleActor::discipleCharm}, {"auto_play", &BattleActor::autoPlay},
    {"can_reflect", &BattleActor::canReflect}, {"set_kakarot", &BattleActor::setKakarot},
    {"set_songoku", &BattleActor::setSongoku}, {"set_kirin", &BattleActor::setKirin},
    {"set_oc_tieu", &BattleActor::setOcTieu}, {"set_ca_dic", &BattleActor::setCaDic},
    {"set_thien_xin_hang", &BattleActor::setThienXinHang}, {"set_pikkoro_daimao", &BattleActor::setPikkoroDaimao}
};
const std::map<std::string, std::int32_t BattleTarget::*> TargetIntFields{
    {"level_boss", &BattleTarget::levelBoss}, {"status", &BattleTarget::status},
    {"percent_miss", &BattleTarget::percentMiss}, {"damage_reduction", &BattleTarget::damageReduction}
};
const std::map<std::string, I BattleTarget::*> TargetLongFields{
    {"last_wake_up_ms", &BattleTarget::lastWakeUpMs}, {"damage_limit", &BattleTarget::damageLimit}
};
const std::map<std::string, bool BattleTarget::*> TargetBoolFields{
    {"protected_by_energy", &BattleTarget::protectedByEnergy}, {"cell_armor", &BattleTarget::cellArmor},
    {"chocolate", &BattleTarget::chocolate}, {"held_as_detainee", &BattleTarget::heldAsDetainee},
    {"xinbato_resistance", &BattleTarget::xinbatoResistance}
};

BattleTarget state(const Json& node, BattleTargetKind kind) {
    require(node.is_object(), "state must be an object");
    for (const auto* required : {"hp","max_hp","mana","max_mana","damage_full","critical_full","stamina","x","y"})
        require(node.contains(required), std::string("state requires ") + required);
    BattleTarget result; result.kind = kind; result.human = kind == BattleTargetKind::Player;
    for (auto it = node.begin(); it != node.end(); ++it) {
        const auto& name = it.key(); const auto& value = it.value();
        if (const auto field = ActorLongFields.find(name); field != ActorLongFields.end()) result.*(field->second) = integer(value,name);
        else if (const auto field = ActorIntFields.find(name); field != ActorIntFields.end()) result.*(field->second) = int32(value,name);
        else if (const auto field = ActorBoolFields.find(name); field != ActorBoolFields.end()) result.*(field->second) = boolean(value,name);
        else if (const auto field = TargetIntFields.find(name); field != TargetIntFields.end()) result.*(field->second) = int32(value,name);
        else if (const auto field = TargetLongFields.find(name); field != TargetLongFields.end()) result.*(field->second) = integer(value,name);
        else if (const auto field = TargetBoolFields.find(name); field != TargetBoolFields.end()) result.*(field->second) = boolean(value,name);
        else if (name == "x") result.position.x = int32(value,name);
        else if (name == "y") result.position.y = int32(value,name);
        else if (name == "options") {
            require(value.is_array() && value.size() <= 256, "options must be an array of at most 256 entries");
            for (const auto& item : value) {
                keys(item,{"id","value"},"option");
                const auto id = int32(item.at("id"),"option id");
                require(id >= 0 && result.options.emplace(id,int32(item.at("value"),"option value")).second, "invalid/duplicate option ID");
            }
        } else if (name == "unsupported_mechanics") {
            require(value.is_array() && value.size() <= 256, "unsupported_mechanics must be a bounded array");
            for (const auto& item : value) require(result.unsupportedMechanics.insert(string(item,name)).second,"duplicate unsupported mechanic");
        } else invalid("unknown state key '" + name + "'");
    }
    return result;
}
Json stateJson(const BattleTarget& b) {
    Json result{{"id",b.id},{"kind",b.kind == BattleTargetKind::Player ? "player" : "mob"},
                {"template_id",b.templateId},{"mob_me",b.mobMe},{"x",b.position.x},{"y",b.position.y}};
    for (const auto& field : ActorLongFields) result[field.first] = b.*(field.second);
    for (const auto& field : ActorIntFields) result[field.first] = b.*(field.second);
    for (const auto& field : ActorBoolFields) result[field.first] = b.*(field.second);
    for (const auto& field : TargetLongFields) result[field.first] = b.*(field.second);
    for (const auto& field : TargetIntFields) result[field.first] = b.*(field.second);
    for (const auto& field : TargetBoolFields) result[field.first] = b.*(field.second);
    result["options"] = Json::array();
    for (const auto& option : b.options) result["options"].push_back({{"id",option.first},{"value",option.second}});
    result["unsupported_mechanics"] = b.unsupportedMechanics;
    return result;
}
Json runtimeJson(const SkillRuntime& r) {
    Json result = Json::object();
    result["last_use_ms"] = r.lastUseMs ? Json(*r.lastUseMs) : Json(nullptr);
    result["override_ms"] = r.cooldownOverrideMs ? Json(*r.cooldownOverrideMs) : Json(nullptr);
    return result;
}
const char* outcome(CombatOutcome value) {
    switch (value) {
    case CombatOutcome::Applied: return "applied";
    case CombatOutcome::Rejected: return "rejected";
    case CombatOutcome::Unsupported: return "unsupported";
    }
    return "unsupported";
}
Json eventsJson(std::vector<CombatEvent> events) {
    auto result = Json::array();
    for (const auto& event : events) result.push_back({{"kind",event.kind},{"target",event.target},{"value",event.value}});
    return result;
}
struct ReplayRandom {
    struct Draw { I min, max, value; };
    bool scripted = false;
    std::vector<Draw> script;
    std::size_t cursor = 0;
    std::uint64_t seed = 0;
    std::mt19937_64 engine;
    Json trace = Json::array();
    I draw(I min, I max) {
        require(min >= 0 && max > min, "random domain must be nonnegative and nonempty");
        require(trace.size() < MaxDraws, "random draw budget exhausted");
        I value = 0;
        if (scripted) {
            require(cursor < script.size(), "random script exhausted");
            const auto& expected = script[cursor];
            require(expected.min == min && expected.max == max, "random domain mismatch at draw " + std::to_string(cursor));
            value = expected.value; ++cursor;
        } else {
            const auto span = static_cast<std::uint64_t>(max) - static_cast<std::uint64_t>(min);
            const auto threshold = (std::uint64_t{0} - span) % span;
            std::uint64_t raw;
            do { raw = engine(); } while (raw < threshold);
            value = min + static_cast<I>(raw % span);
        }
        trace.push_back({{"min",min},{"max",max},{"value",value}});
        return value;
    }
};
std::shared_ptr<ReplayRandom> randomSource(const Json& node) {
    keys(node,{"seed","draws"},"random");
    require(node.contains("seed") != node.contains("draws"), "random requires exactly one of seed or draws");
    auto result = std::make_shared<ReplayRandom>();
    if (node.contains("seed")) {
        const auto& seed = node.at("seed");
        require(seed.is_number_integer() && (seed.is_number_unsigned() || seed.get<I>() >= 0), "random seed must be a nonnegative uint64 integer");
        result->seed = seed.get<std::uint64_t>(); result->engine.seed(result->seed);
    } else {
        result->scripted = true;
        for (const auto& draw : array(node,"draws",MaxDraws)) {
            keys(draw,{"min","max","value"},"random draw");
            ReplayRandom::Draw item{integer(draw.at("min"),"draw min",0), integer(draw.at("max"),"draw max",1), integer(draw.at("value"),"draw value",0)};
            require(item.min < item.max && item.value >= item.min && item.value < item.max,"invalid scripted draw interval/value");
            result->script.push_back(item);
        }
    }
    return result;
}
void addActors(LegacyWorld& world, const Json& nodes) {
    for (const auto& node : nodes) {
        keys(node,{"id","class_id","selected_skill_id","learned","cooldowns","state"},"actor");
        LegacyActorState actor;
        actor.battle = state(node.at("state"),BattleTargetKind::Player);
        actor.battle.id = integer(node.at("id"),"actor id",0);
        actor.classId = int32(node.at("class_id"),"class_id");
        actor.selectedSkillId = int32(node.at("selected_skill_id"),"selected_skill_id");
        for (const auto& learned : array(node,"learned",256)) {
            keys(learned,{"skill_id","point"},"learned skill");
            require(actor.learned.emplace(int32(learned.at("skill_id"),"skill_id"),int32(learned.at("point"),"point")).second,
                    "duplicate learned skill ID");
        }
        if (node.contains("cooldowns")) for (const auto& saved : array(node,"cooldowns",256)) {
            keys(saved,{"skill_id","last_use_ms","override_ms"},"saved cooldown");
            SkillRuntime runtime;
            if (saved.contains("last_use_ms")) runtime.lastUseMs = integer(saved.at("last_use_ms"),"last_use_ms",0);
            if (saved.contains("override_ms")) runtime.cooldownOverrideMs = static_cast<std::int32_t>(integer(saved.at("override_ms"),"override_ms",0,(std::numeric_limits<std::int32_t>::max)()));
            require(actor.skillRuntimes.emplace(int32(saved.at("skill_id"),"cooldown skill_id"),runtime).second,"duplicate saved cooldown");
        }
        world.addActor(std::move(actor));
    }
}
} // namespace

Json runReplay(std::shared_ptr<const ContentSnapshot> content, const Json& scenario) {
    keys(scenario,{"format","version","note","map_id","start_time_ms","actors","mobs","random","operations"},"scenario");
    require(scenario.at("format") == "hunr-legacy-replay", "unsupported scenario format");
    require(integer(scenario.at("version"),"version") == 1,"unsupported scenario version");
    if (scenario.contains("note")) string(scenario.at("note"),"note");
    const auto& actors = array(scenario,"actors",MaxEntities);
    const auto& mobs = array(scenario,"mobs",MaxEntities);
    require(actors.size() + mobs.size() <= MaxEntities,"too many scenario entities");
    const auto& operations = array(scenario,"operations",MaxSteps);
    auto random = randomSource(scenario.at("random"));
    LegacyWorld world(std::move(content),int32(scenario.at("map_id"),"map_id"),[random](I min,I max){ return random->draw(min,max); });
    const I startTime = integer(scenario.at("start_time_ms"),"start_time_ms",0);
    require(static_cast<bool>(world.advance(startTime)),"invalid start time");
    addActors(world,actors);
    for (const auto& node : mobs) {
        keys(node,{"id","template_id","state"},"mob");
        auto mob = state(node.at("state"),BattleTargetKind::Mob);
        mob.id = integer(node.at("id"),"mob id",0); mob.templateId = int32(node.at("template_id"),"template_id");
        world.addMob(std::move(mob));
    }
    Json trace = Json::array();
    std::size_t index = 0;
    for (const auto& operation : operations) {
        require(operation.is_object(),"operation must be an object");
        const auto name = string(operation.at("op"),"operation op");
        if (name == "advance") keys(operation,{"op","at_ms"},name);
        else if (name == "select_skill") keys(operation,{"op","at_ms","actor","skill_id"},name);
        else if (name == "attack_mob") keys(operation,{"op","at_ms","actor","target","summoned_target","summon_owner_target_allowed"},name);
        else if (name == "attack_player") keys(operation,{"op","at_ms","actor","target","pvp_allowed"},name);
        else if (name == "nonfocus") keys(operation,{"op","at_ms","actor","action_type"},name);
        else if (name == "move") keys(operation,{"op","at_ms","actor","x","y","flight"},name);
        else if (name == "remove_actor") keys(operation,{"op","at_ms","actor"},name);
        else invalid("unknown operation '" + name + "'");
        const I at = integer(operation.at("at_ms"),"at_ms",0);
        const I actor = name == "advance" ? 0 : integer(operation.at("actor"),"operation actor",0);
        const I target = name == "attack_mob" || name == "attack_player" ? integer(operation.at("target"),"target",0) : 0;
        const int skill = name == "select_skill" ? int32(operation.at("skill_id"),"skill_id") : 0;
        const int action = name == "nonfocus" ? int32(operation.at("action_type"),"action_type") : 0;
        const bool summoned = name == "attack_mob" && operation.contains("summoned_target") && boolean(operation.at("summoned_target"),"summoned_target");
        const bool summonAllowed = name == "attack_mob" && operation.contains("summon_owner_target_allowed") && boolean(operation.at("summon_owner_target_allowed"),"summon_owner_target_allowed");
        const bool pvpAllowed = name == "attack_player" && boolean(operation.at("pvp_allowed"),"pvp_allowed");
        const bool flight = name == "move" && boolean(operation.at("flight"),"flight");
        const BattlePosition position = name == "move" ? BattlePosition{int32(operation.at("x"),"x"),int32(operation.at("y"),"y")} : BattlePosition{};
        Json row{{"index",index++},{"op",name},{"at_ms",at}};
        auto result = world.advance(at);
        std::optional<AttackResult> attack;
        if (result && name != "advance") {
            if (name == "select_skill") result = world.selectSkill(actor,skill);
            else if (name == "attack_mob") attack = world.attackMob(actor,target,summoned,summonAllowed);
            else if (name == "attack_player") attack = world.attackPlayer(actor,target,pvpAllowed);
            else if (name == "nonfocus") attack = world.useNonFocus(actor,action);
            else if (name == "move") result = world.move(actor,position,flight);
            else if (name == "remove_actor") result = world.removeActor(actor) ? WorldOperation{CombatOutcome::Applied,"removed"} : WorldOperation{CombatOutcome::Rejected,"actor_missing"};
            if (attack) {
                result = {attack->outcome,attack->reason};
                row["damage"] = attack->damage; row["critical"] = attack->critical; row["miss"] = attack->miss;
            }
        }
        row["outcome"] = outcome(result.outcome); row["reason"] = result.reason;
        row["events"] = eventsJson(world.takeEvents());
        trace.push_back(std::move(row));
    }
    Json finalActors = Json::array(), finalMobs = Json::array(), finalSummons = Json::array(), effects = Json::array();
    for (const auto& entry : world.actors()) {
        const auto& actor = entry.second;
        Json value{{"state",stateJson(actor.battle)},{"class_id",actor.classId},{"selected_skill_id",actor.selectedSkillId}};
        value["learned"] = Json::array(); value["cooldowns"] = Json::array();
        for (const auto& learned : actor.learned) value["learned"].push_back({{"skill_id",learned.first},{"point",learned.second}});
        for (const auto& runtime : actor.skillRuntimes) { auto saved = runtimeJson(runtime.second); saved["skill_id"] = runtime.first; value["cooldowns"].push_back(std::move(saved)); }
        finalActors.push_back(std::move(value));
        if (const auto* summon = world.summon(entry.first))
            finalSummons.push_back({{"owner",summon->owner},{"damage_percent",summon->damagePercent},{"state",stateJson(summon->battle)}});
    }
    for (const auto& entry : world.mobs()) finalMobs.push_back(stateJson(entry.second));
    for (const auto& effect : world.scheduledEffects()) effects.push_back({{"token",effect.token},{"source",effect.source},
        {"kind",effect.recipe.kind},{"target",effect.recipe.target},{"target_kind",effect.targetKind == BattleTargetKind::Player ? "player" : "mob"},
        {"target_is_summon",effect.targetIsSummon},{"starts_at_ms",effect.startsAt},{"expires_at_ms",effect.expiresAt},{"started",effect.started}});
    Json randomReport{{"mode",random->scripted ? "scripted" : "seeded"},{"draws_used",random->trace.size()},{"trace",std::move(random->trace)}};
    if (random->scripted) randomReport["unused_draws"] = random->script.size() - random->cursor;
    else randomReport["seed"] = random->seed;
    return {{"format","hunr-legacy-replay-result"},{"version",1},{"map_id",world.mapId()},
            {"steps",std::move(trace)},{"random",std::move(randomReport)},
            {"final",{{"time_ms",world.nowMs()},{"actors",std::move(finalActors)},{"mobs",std::move(finalMobs)},
                       {"summons",std::move(finalSummons)},{"effects",std::move(effects)}}}};
}
} // namespace game::legacy
