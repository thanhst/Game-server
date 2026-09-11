#include "game/legacy/LegacyWorld.h"
#include <algorithm>
#include <stdexcept>

#ifndef GAME_HUNR_CONTENT_FILE
#define GAME_HUNR_CONTENT_FILE "content/hunr/content.json"
#endif

namespace {
using namespace game::legacy;
void worldCheck(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(std::string("LegacyWorldTests: ") + message);
}
LegacyActorState player(std::int64_t id, int classId, std::map<int,int> skills, int selected) {
    LegacyActorState actor;
    actor.classId = classId; actor.learned = std::move(skills); actor.selectedSkillId = selected;
    auto& b = actor.battle;
    b.id = id; b.kind = BattleTargetKind::Player; b.hp = b.maxHp = 1000;
    b.mana = b.maxMana = 50000; b.damageFull = 100; b.stamina = 100; b.position = {100,100};
    return actor;
}
BattleTarget creature(std::int64_t id) {
    BattleTarget b;
    b.id = id; b.kind = BattleTargetKind::Mob; b.human = false; b.templateId = 1;
    b.hp = b.maxHp = 1000; b.position = {110,100};
    return b;
}
auto highDraw() { return [](std::int64_t, std::int64_t max) { return max - 1; }; }
} // namespace

void runLegacyWorldTests() {
    using namespace game::legacy;
    const auto content = std::make_shared<const ContentSnapshot>(ContentSnapshot::load(GAME_HUNR_CONTENT_FILE));
    {
        LegacyWorld world(content, 0, highDraw());
        world.addActor(player(1,0,{{0,1},{19,1},{22,1}},0));
        world.addActor(player(2,0,{{0,1}},0));
        world.addMob(creature(1)); // Mob ID intentionally collides with player ID.
        worldCheck(static_cast<bool>(world.advance(10000)), "initial clock advance");
        worldCheck(world.attackMob(1,1).damage == 99, "real SQL Dragon damage and RNG domain");
        worldCheck(world.actor(1)->battle.mana == 49999 && world.mob(1)->hp == 901, "authoritative commit");
        worldCheck(world.attackMob(1,1).outcome == CombatOutcome::Rejected, "per-actor cooldown enforced");
        worldCheck(world.attackMob(2,1).outcome == CombatOutcome::Applied, "cooldown does not leak between actors");
        worldCheck(world.selectSkill(1,123456).outcome == CombatOutcome::Rejected, "unlearned skill rejected");
        world.selectSkill(1,22);
        worldCheck(world.attackMob(1,1).outcome == CombatOutcome::Applied && world.mob(1)->sleeping, "sleep recipe applied to mob namespace");
        worldCheck(!world.actor(1)->battle.sleeping, "same-numbered player unaffected by mob effect");
        world.advance(14999); worldCheck(world.mob(1)->sleeping, "sleep alive before expiry");
        world.advance(15000); worldCheck(!world.mob(1)->sleeping, "sleep expires at deadline");
        worldCheck(world.advance(14999).outcome == CombatOutcome::Rejected && world.nowMs() == 15000, "clock rollback rejected atomically");
        worldCheck(!world.takeEvents().empty() && world.takeEvents().empty(), "events drain once");
    }
    {
        LegacyWorld world(content,0,highDraw());
        world.addActor(player(1,0,{{19,1},{22,1}},19)); world.addMob(creature(2)); world.advance(10000);
        worldCheck(world.useNonFocus(1,9).outcome == CombatOutcome::Applied, "real shield skill accepted");
        worldCheck(world.actor(1)->battle.protectedByEnergy && world.actor(1)->battle.mana == 24500, "51 percent max MP shield cost from SQL");
        world.advance(24999); worldCheck(world.actor(1)->battle.protectedByEnergy, "shield before expiry");
        world.advance(25000); worldCheck(!world.actor(1)->battle.protectedByEnergy, "shield expiration clears protection");
        world.selectSkill(1,22); world.attackMob(1,2);
        worldCheck(world.mob(2)->sleeping, "post-shield sleep setup");
        world.removeActor(1);
        worldCheck(world.mob(2)->sleeping, "independent timed target debuff survives caster disconnect");
        world.advance(30000); worldCheck(!world.mob(2)->sleeping, "orphan-caster debuff still expires");
    }
    {
        LegacyWorld world(content,0,highDraw());
        world.addActor(player(3,1,{{2,1},{12,1}},12)); world.addMob(creature(2)); world.advance(10000);
        worldCheck(world.useNonFocus(3,8).outcome == CombatOutcome::Applied, "summon cast commits");
        worldCheck(world.summon(3) && world.summon(3)->battle.maxHp == 1000 && world.summon(3)->battle.templateId == 8 &&
                   world.summon(3)->battle.position.y == 60, "actual summoned mob source fields");
        world.selectSkill(3,2);
        const auto attack = world.attackMob(3,2);
        worldCheck(attack.damage == 94 && world.mob(2)->hp == 856, "source Demon damage plus owned summon follow-up");
        worldCheck(world.attackMob(3,3,true).outcome == CombatOutcome::Rejected, "summon owner admission is explicit");
        world.advance(74999); worldCheck(world.summon(3) != nullptr, "summon before lifetime end");
        world.advance(75000); worldCheck(world.summon(3) == nullptr, "summon lifetime expiry removes owned mob");
    }
    {
        LegacyWorld world(content,0,highDraw());
        world.addActor(player(4,2,{{13,1}},13)); world.advance(10000);
        const auto refused = world.useNonFocus(4,6);
        worldCheck(refused.outcome == CombatOutcome::Unsupported && !world.actor(4)->battle.charging &&
                   world.actor(4)->skillRuntimes.empty(), "missing transform stat pipeline does not partially commit");
        world.setStatResolver([](const LegacyActorState& actor) {
            auto b = actor.battle;
            b.maxHp = b.monkey ? 2000 : 1000; b.maxMana = b.monkey ? 100000 : 50000;
            b.damageFull = b.monkey ? 105 : 100; b.criticalFull = b.monkey ? 100 : 0;
            b.hp = (std::min)(b.hp,b.maxHp); b.mana = (std::min)(b.mana,b.maxMana);
            return b;
        });
        worldCheck(world.useNonFocus(4,6).outcome == CombatOutcome::Applied && world.actor(4)->battle.charging, "transform schedules charge");
        world.advance(12999); worldCheck(!world.actor(4)->battle.monkey, "transform delay preserved");
        world.advance(13000);
        worldCheck(world.actor(4)->battle.monkey && !world.actor(4)->battle.charging && world.actor(4)->battle.hp == 2000,
                   "delayed transform resolves stats and recovers fifty percent");
        world.advance(78000); worldCheck(!world.actor(4)->battle.monkey && world.actor(4)->battle.maxHp == 1000, "transform expiry resolves base state");
        worldCheck(world.move(4,{120,100},false).outcome == CombatOutcome::Unsupported, "missing terrain data has explicit movement failure");
    }
    {
        LegacyWorld world(content,0,highDraw()); world.addActor(player(1,0,{{0,1}},0)); world.advance(10000);
        RuleProfile profile;
        profile.registerRule("legacy.nonfocus", [](const AttackContext& c) {
            AttackResult r; r.actor = c.actor; r.target = c.target; r.runtime = c.runtime; r.outcome = CombatOutcome::Applied;
            r.effects.push_back({"test_bonus",c.actor.id,1000,0,7,-1}); return r;
        });
        world.setRuleProfile(std::move(profile));
        worldCheck(world.useNonFocus(1,0).outcome == CombatOutcome::Unsupported && world.actor(1)->battle.damageFull == 100,
                   "unknown recipe preflight prevents partial commit");
        world.registerEffectRule("test_bonus", [](BattleTarget& b, const EffectRecipe& recipe, bool start) {
            b.damageFull += start ? recipe.magnitude : -recipe.magnitude; return WorldOperation{CombatOutcome::Applied,"applied"};
        });
        worldCheck(world.useNonFocus(1,0).outcome == CombatOutcome::Applied && world.actor(1)->battle.damageFull == 107, "custom effect applied");
        world.advance(11000); worldCheck(world.actor(1)->battle.damageFull == 100, "custom effect expiry");
    }
}
