#pragma once

#include "game/legacy/SkillRules.h"
#include <functional>
#include <map>
#include <set>

namespace game::legacy {
struct BattlePosition { std::int32_t x = 0; std::int32_t y = 0; };
struct BattleActor {
    std::int64_t id = 0;
    BattlePosition position;
    std::int64_t hp = 0, maxHp = 0, mana = 0, maxMana = 0, damageFull = 0;
    std::int32_t criticalFull = 0, stamina = 0;
    std::map<std::int32_t, std::int32_t> options;
    std::int32_t specialId = -1, specialParam = 0, percentDamageBonus = 0;
    std::int64_t optionKame = 0, optionLaze = 0; // Info.java accumulates these in long fields.
    std::int32_t mapPhuHo = -1, flag = 0, satellites343 = 0;
    std::int64_t lastAttackMs = 0, lastXChuongMs = 0, lastRecoveryMs = 0, lastRecoveryStartedMs = 0;
    bool dead = false, frozen = false, sleeping = false, held = false, stone = false, blind = false;
    bool human = true, boss = false, disciple = false, discipleType2 = false;
    bool critFirstHit = false, skillSpecial = false, recovering = false, charging = false, monkey = false;
    bool immortalCharm = false, powerfulCharm = false, enduranceCharm = false;
    bool heroicCharm = false, discipleCharm = false, autoPlay = false, canReflect = true;
    bool setKakarot = false, setSongoku = false, setKirin = false, setOcTieu = false;
    bool setCaDic = false, setThienXinHang = false, setPikkoroDaimao = false;
    // Populate for a subclass/side-system not represented by this snapshot.
    std::set<std::string> unsupportedMechanics;
};
enum class BattleTargetKind { Mob, Player };
struct BattleTarget : BattleActor {
    BattleTargetKind kind = BattleTargetKind::Mob;
    std::int32_t templateId = -1, levelBoss = 0, status = 4;
    std::int32_t percentMiss = 0, damageReduction = 0;
    std::int64_t lastWakeUpMs = 0, damageLimit = -1;
    bool mobMe = false, protectedByEnergy = false, cellArmor = false, chocolate = false;
    bool heldAsDetainee = false, xinbatoResistance = false;
};
struct BattleMap {
    std::int32_t id = -1;
    bool pet = false, nappa = false, cold = false, future = false, barrack = false, baseBabidi = false;
};
// Equivalent integer draw domains to Utils.nextInt(max)/nextLong(min,max).
// The callback must return min <= value < max. No call occurs when min == max.
using RandomDraw = std::function<std::int64_t(std::int64_t min, std::int64_t max)>;
struct AttackContext {
    BattleActor actor;
    BattleTarget target;
    SkillTemplate skill;
    SkillLevel level;
    SkillRuntime runtime;
    std::int64_t nowMs = 0;
    RandomDraw random;
    BattleMap map;
    bool playerTargetAllowed = false; // Result of Java PvP/ownership admission supplied by adapter.
    // getTotalHP(): current human-player HP + all zone-mob HP, not maximum HP.
    std::optional<std::int64_t> totalZoneHp;
    bool completeZoneMobs = false;
    std::vector<BattleTarget> zoneMobs;
    std::int32_t nonFocusType = -1;
};
enum class CombatOutcome { Applied, Rejected, Unsupported };
struct CombatEvent {
    std::string kind;
    std::int64_t target = 0;
    std::int64_t value = 0;
};
struct EffectRecipe {
    std::string kind;
    std::int64_t target = 0;
    std::int64_t durationMs = 0;
    std::int64_t delayMs = 0;
    std::int64_t magnitude = 0;
    std::int32_t templateId = -1;
};
struct AttackResult {
    CombatOutcome outcome = CombatOutcome::Rejected;
    std::string reason;
    BattleActor actor;
    BattleTarget target;
    SkillRuntime runtime;
    std::vector<BattleTarget> zoneMobs;
    std::vector<CombatEvent> events;
    std::vector<EffectRecipe> effects;
    std::int64_t damage = 0;
    bool critical = false, miss = false;
};

// Pure snapshot evaluation: caller commits returned mutations and executes recipes/events.
// Rejected/Unsupported results retain original snapshots and contain no events/recipes.
AttackResult attackMob(const AttackContext& context);
AttackResult attackPlayer(const AttackContext& context);
AttackResult useNonFocusSkill(const AttackContext& context);

// Separate game profiles can register new mechanics without editing legacy switches.
class RuleProfile {
public:
    using Rule = std::function<AttackResult(const AttackContext&)>;
    void registerRule(std::string key, Rule rule);
    AttackResult evaluate(const std::string& key, const AttackContext& context) const;
private:
    std::map<std::string, Rule> rules_;
};
RuleProfile hunr2026Rules(); // legacy.mob, legacy.player, legacy.nonfocus
} // namespace game::legacy
