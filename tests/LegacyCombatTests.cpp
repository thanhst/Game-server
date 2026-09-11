#include "game/legacy/CombatRules.h"
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
using namespace game::legacy;
using I = std::int64_t;
void combatCheck(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(std::string("LegacyCombatTests: ") + reason);
}
struct DrawStep { I min, max, value; };
struct Script {
    std::vector<DrawStep> steps;
    std::size_t next = 0;
    I draw(I min, I max) {
        combatCheck(next < steps.size(), "unexpected random draw");
        const auto step = steps[next++];
        combatCheck(min == step.min && max == step.max, "random domain/order differs from Java");
        return step.value;
    }
};
std::shared_ptr<Script> random(AttackContext& c, std::vector<DrawStep> steps) {
    auto script = std::make_shared<Script>(); script->steps = std::move(steps);
    c.random = [script](I min, I max) { return script->draw(min, max); };
    return script;
}
AttackContext base() {
    AttackContext c;
    c.nowMs = 10000;
    c.actor.id = 1; c.actor.hp = c.actor.maxHp = 1000;
    c.actor.mana = c.actor.maxMana = 500; c.actor.damageFull = 100; c.actor.stamina = 10;
    c.target.id = 2; c.target.hp = c.target.maxHp = 1000;
    c.target.templateId = 1; c.target.position = {10, 0};
    c.skill.id = SkillId::CHIEU_DAM_DRAGON; c.skill.name = "Imported example"; c.skill.type = 1;
    c.level.id = 17; c.level.point = 1; c.level.damage = 100;
    c.level.dx = 100; c.level.cooldownMs = 1000; c.level.manaUse = 10;
    return c;
}
void ordinaryDraws(AttackContext& c, I min = 90, I max = 100, I hit = 95) {
    random(c, {{0,10,1}, {0,100,99}, {min,max,hit}, {0,100,99}});
}
void playerDraws(AttackContext& c, I miss = 99) {
    random(c, {{0,100,miss}, {0,100,99}, {90,100,95}, {0,100,99}});
}
bool hasEvent(const AttackResult& r, const std::string& kind, I value) {
    for (const auto& event : r.events) if (event.kind == kind && event.value == value) return true;
    return false;
}
} // namespace

void runLegacyCombatTests() {
    using namespace game::legacy;
    {
        auto c = base();
        const auto script = random(c, {{0,10,1}, {0,100,99}, {90,100,95}, {0,100,99}});
        const auto r = attackMob(c);
        combatCheck(r.outcome == CombatOutcome::Applied && r.damage == 95 && r.target.hp == 905, "normal mob damage");
        combatCheck(r.actor.mana == 490 && r.actor.stamina == 9 && r.runtime.lastUseMs == c.nowMs, "cost/cooldown/stamina order");
        combatCheck(hasEvent(r, "add_power_and_potential_input", 9), "experience stage input");
        combatCheck(script->next == script->steps.size(), "all expected draws consumed");
        combatCheck(c.target.hp == 1000 && c.actor.mana == 500, "input snapshot mutated");
    }
    {
        auto c = base(); c.runtime.recordUse(9500);
        const auto r = attackMob(c);
        combatCheck(r.outcome == CombatOutcome::Rejected && r.reason == "cooldown", "cooldown rejects before RNG");
        combatCheck(r.events.empty() && r.actor.mana == c.actor.mana, "rejection is transactional");
        c.runtime.recordUse(9000); ordinaryDraws(c);
        combatCheck(attackMob(c).outcome == CombatOutcome::Applied, "exact cooldown boundary");
    }
    {
        auto c = base(); c.actor.damageFull = 101; c.level.damage = 50;
        ordinaryDraws(c, 46, 51, 50);
        combatCheck(attackMob(c).damage == 50, "signed percentage truncation before random is source faithful");
        c = base(); c.target.position = {150, 0}; ordinaryDraws(c);
        combatCheck(attackMob(c).damage == 95, "range plus fifty inclusive boundary");
        c = base(); c.target.position = {151, 0}; ordinaryDraws(c);
        const auto out = attackMob(c);
        combatCheck(out.damage == 0 && out.actor.mana == 490 && out.runtime.lastUseMs == c.nowMs, "out-of-range mob hit still spends cost and cooldown");
    }
    {
        auto c = base(); c.actor.damageFull = 2000; ordinaryDraws(c,1800,2000,1900);
        auto r = attackMob(c);
        combatCheck(r.damage == 999 && r.target.hp == 1, "full-health one-shot prevention");
        c.target.hp = 900; ordinaryDraws(c,1800,2000,1900); r = attackMob(c);
        combatCheck(r.damage == 900 && r.target.dead, "partial-health mob may die");
        c.target.hp = 1000; c.target.levelBoss = 1; ordinaryDraws(c,1800,2000,1900);
        combatCheck(attackMob(c).damage == 100, "boss mob ten-percent cap follows one-shot cap");
        c.actor.heroicCharm = true; ordinaryDraws(c,1800,2000,1900);
        combatCheck(attackMob(c).damage == 999, "heroic charm bypasses boss cap only");
    }
    {
        auto c = base(); c.actor.criticalFull = 100; c.actor.options[19] = 20; c.actor.options[5] = 10;
        ordinaryDraws(c);
        combatCheck(attackMob(c).damage == 250, "mob bonus then critical multiplier then critical option rounding");
        c = base(); c.actor.critFirstHit = true;
        random(c, {{0,10,0}, {0,100,99}, {90,100,95}, {0,100,99}});
        const auto missed = attackMob(c);
        combatCheck(missed.damage == -1 && missed.actor.mana == 490 && !missed.actor.critFirstHit, "mob miss sentinel and consumed first critical");
    }
    {
        auto c = base(); c.actor.skillSpecial = true; c.runtime.recordUse(9999); c.target.position = {2000,0};
        random(c, {{0,10,0}, {0,100,0}, {90,100,95}, {0,100,0}});
        auto r = attackMob(c);
        combatCheck(r.damage == 95 && r.actor.mana == 500 && r.runtime.lastUseMs == 9999 && !r.actor.skillSpecial,
                    "special attack bypasses cooldown/range/miss/cost, then clears flag");
        c = base(); c.target.templateId = 70; c.target.position = {2000,0}; ordinaryDraws(c);
        combatCheck(attackMob(c).damage == 10, "template70 source override occurs after range");
    }
    {
        auto c = base(); c.skill.id = SkillId::MAKANKOSAPPO; c.level.damage = 200;
        ordinaryDraws(c,180,200,190);
        const auto r = attackMob(c);
        combatCheck(r.damage == 999 && r.actor.mana == -9, "Makankosappo uses current mana, sets one, then source tail subtracts cost");
        c = base(); c.skill.id = SkillId::QUA_CAU_KENH_KHI; ordinaryDraws(c);
        auto noZone = attackMob(c);
        combatCheck(noZone.outcome == CombatOutcome::Unsupported && noZone.actor.mana == 500, "missing spirit-bomb area snapshot is explicit");
        c.totalZoneHp = 10000; c.completeZoneMobs = true; c.zoneMobs.push_back(c.target); ordinaryDraws(c);
        const auto area = attackMob(c);
        combatCheck(area.damage == 999 && area.target.hp == -998, "source spirit-bomb area loop hits surviving primary again");
    }
    {
        auto c = base(); c.skill.id = SkillId::THOI_MIEN; c.level.damage = 5;
        random(c, {{0,10,1}, {0,100,99}});
        const auto sleep = attackMob(c);
        combatCheck(sleep.target.sleeping && sleep.effects.at(0).durationMs == 5000 && !sleep.runtime.lastUseMs,
                    "mob sleep early return has duration and no cooldown update");
        c = base(); c.skill.id = SkillId::TROI; c.level.damage = 5;
        // Equal random bounds do not consume a damage draw.
        random(c, {{0,10,1}, {0,100,99}, {0,100,99}});
        const auto hold = attackMob(c);
        combatCheck(hold.actor.held && hold.target.held && hold.actor.critFirstHit && hold.target.hp == 1000,
                    "hold is a useSkill result, not direct damage");
    }
    {
        auto c = base(); c.target.kind = BattleTargetKind::Player; c.playerTargetAllowed = true; playerDraws(c);
        combatCheck(attackPlayer(c).target.hp == 905, "ordinary player attack");
        c.target.percentMiss = 100; c.target.options[97] = 50; playerDraws(c);
        const auto reflection = attackPlayer(c);
        combatCheck(reflection.damage == 0 && reflection.actor.hp == 953, "reflection precedes miss filtering in Java");
        c = base(); c.target.kind = BattleTargetKind::Player; c.playerTargetAllowed = true;
        c.target.protectedByEnergy = true; c.target.position = {1000,0}; playerDraws(c);
        combatCheck(attackPlayer(c).damage == 1, "duplicate PvP shield branch follows range check");
        c.target.hp = 1; playerDraws(c);
        combatCheck(attackPlayer(c).damage == 0, "PvP shield avoids killing at one HP");
        c.target.boss = true;
        combatCheck(attackPlayer(c).outcome == CombatOutcome::Unsupported, "unported boss subclass cannot silently use player rules");
    }
    {
        auto c = base(); c.skill.type = 3; c.skill.id = SkillId::KHIEN_NANG_LUONG; c.nonFocusType = 9; c.level.point = 3;
        auto r = useNonFocusSkill(c);
        combatCheck(r.effects.at(0).kind == "energy_protection" && r.effects.at(0).durationMs == 25000 && r.actor.mana == 490,
                    "source energy protection recipe");
        c.skill.id = SkillId::BIEN_HINH; c.nonFocusType = 6; c.actor.setCaDic = true;
        r = useNonFocusSkill(c);
        combatCheck(r.effects.at(0).durationMs == 425000 && r.effects.at(0).delayMs == 3000 && r.actor.mana == 500,
                    "transform delay/set multiplier and source no-cost-deduction branch");
        c.skill.id = SkillId::DE_TRUNG; c.nonFocusType = 8; c.level.point = 7;
        r = useNonFocusSkill(c);
        combatCheck(r.effects.at(0).magnitude == 7000 && r.effects.at(0).templateId == 50 && r.effects.at(0).durationMs == 125000,
                    "summon source HP/template/lifetime recipe");
        c.skill.id = SkillId::THAI_DUONG_HA_SAN; c.nonFocusType = 0;
        combatCheck(useNonFocusSkill(c).outcome == CombatOutcome::Unsupported, "unported area crowd control explicit");
    }
    {
        auto c = base(); c.actor.unsupportedMechanics.insert("active_summon_ai");
        combatCheck(attackMob(c).outcome == CombatOutcome::Unsupported, "unsupported actor subsystem marker");
        c = base(); c.random = [](I, I max) { return max; };
        combatCheck(attackMob(c).reason == "random_draw_out_of_range", "invalid random callback rejected");
        auto profile = hunr2026Rules();
        combatCheck(profile.evaluate("new.rule", c).outcome == CombatOutcome::Unsupported, "unknown rules never fall back silently");
        profile.registerRule("new.rule", [](const AttackContext& context) { AttackResult r; r.actor = context.actor; r.outcome = CombatOutcome::Applied; r.reason = "custom"; return r; });
        combatCheck(profile.evaluate("new.rule", c).reason == "custom", "separate custom rule registry extension");
    }
}
