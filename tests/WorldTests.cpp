#include "game/World.h"

#include <cmath>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool close(double a, double b) { return std::abs(a - b) < 1e-8; }
game::Content fixture() {
    game::Content c;
    c.maps.emplace("arena", game::MapDefinition{"arena", 1000, 1000});
    game::EffectDefinition hit;
    hit.id = "hit"; hit.handler = "damage"; hit.magnitude = 10;
    hit.scalingAttribute = "attack"; hit.scalingFactor = 1;
    c.effects.emplace(hit.id, hit);
    auto poison = hit;
    poison.id = "poison"; poison.handler = "periodic_damage";
    poison.magnitude = 10; poison.scalingAttribute.clear(); poison.scalingFactor = 0;
    poison.duration = 3000; poison.period = 1000;
    poison.stacking = game::Stacking::Stack; poison.maxStacks = 3;
    c.effects.emplace(poison.id, poison);
    game::EffectDefinition buff;
    buff.id = "buff"; buff.handler = "modifier"; buff.magnitude = 20;
    buff.attribute = "attack"; buff.duration = 1500;
    c.effects.emplace(buff.id, buff);
    auto stun = buff;
    stun.id = "stun"; stun.handler = "control"; stun.attribute.clear();
    stun.tags = {"stunned"}; stun.duration = 1000;
    c.effects.emplace(stun.id, stun);
    auto shield = buff;
    shield.id = "shield"; shield.handler = "shield"; shield.attribute.clear();
    shield.magnitude = 50; shield.duration = 3000;
    c.effects.emplace(shield.id, shield);
    auto heal = hit;
    heal.id = "heal"; heal.handler = "heal"; heal.magnitude = 100;
    heal.scalingAttribute.clear(); heal.scalingFactor = 0;
    c.effects.emplace(heal.id, heal);
    game::SkillDefinition attack;
    attack.id = "attack"; attack.name = "Attack"; attack.effects = {"hit"};
    attack.manaCost = 10; attack.cooldown = 500; attack.range = 50;
    c.skills.emplace(attack.id, attack);
    for (const auto& id : {"poison", "buff", "stun", "shield", "heal"}) {
        auto skill = attack;
        skill.id = id; skill.name = id; skill.effects = {id}; skill.cooldown = 0;
        if (skill.id == "buff" || skill.id == "shield") skill.target = game::TargetRule::Self;
        if (skill.id == "heal") skill.target = game::TargetRule::Ally;
        c.skills.emplace(skill.id, skill);
    }
    game::CharacterDefinition hero;
    hero.id = "hero"; hero.name = "Hero";
    hero.attributes = {{"hp", 1000}, {"mana", 200}, {"attack", 20}, {"defense", 0}, {"speed", 100}};
    hero.tags = {"player"};
    hero.skills = {"attack", "poison", "buff", "stun", "shield", "heal"};
    c.characters.emplace(hero.id, hero);
    return c;
}
void rejectionAndMovement() {
    game::World world(fixture());
    const auto a = world.spawn("hero", "arena", 1, {100, 100});
    const auto b = world.spawn("hero", "arena", 2, {120, 100});
    check(world.cast(a, "attack", b).ok, "initial attack rejected");
    check(close(world.find(b)->hp, 970), "damage calculation");
    check(close(world.find(a)->mana, 190), "cost not charged once");
    check(!world.cast(a, "attack", b).ok, "cooldown bypass");
    check(close(world.find(a)->mana, 190), "rejected cast charged cost");
    check(!world.cast(a, "attack", a).ok, "friendly attack accepted");
    check(!world.cast(a, "unlearned", b).ok, "unknown skill accepted");
    world.advance(500);
    check(world.move(a, {150, 100}).ok, "legal speed rejected");
    check(!world.move(a, {151, 100}).ok, "same-tick speed multiplication");
    check(!world.move(a, {-1, 100}).ok, "out of map movement");
    world.advance(1000);
    check(world.cast(a, "stun", b).ok, "stun rejected");
    check(!world.move(b, {121, 100}).ok, "stun allowed movement");
    check(!world.cast(b, "attack", a).ok, "stun allowed skill");
    world.advance(2000);
    check(world.cast(b, "attack", a).ok, "stun failed to expire");
    bool rejected = false;
    try { world.advance(1999); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "clock reversal accepted");
}
void tickingAndStacking() {
    game::World one(fixture()), many(fixture());
    for (auto* w : {&one, &many}) {
        const auto a = w->spawn("hero", "arena", 1, {0, 0});
        const auto b = w->spawn("hero", "arena", 2, {10, 0});
        check(w->cast(a, "poison", b).ok, "poison rejected");
        check(close(w->find(b)->hp, 1000), "poison ticked early");
    }
    one.advance(3000);
    for (game::Milliseconds t = 50; t <= 3000; t += 50) many.advance(t);
    check(close(one.find(2)->hp, 970), "final expiry-time tick missing");
    check(close(one.find(2)->hp, many.find(2)->hp), "large-step periodic divergence");
    check(one.find(2)->effects.empty(), "periodic effect did not expire");

    game::World world(fixture());
    const auto a = world.spawn("hero", "arena", 1, {0, 0});
    const auto b = world.spawn("hero", "arena", 2, {10, 0});
    const auto ally = world.spawn("hero", "arena", 1, {0, 0});
    check(world.cast(a, "poison", b).ok, "first poison");
    world.advance(500);
    check(world.cast(a, "poison", b).ok, "stack poison");
    check(world.cast(ally, "poison", b).ok, "second source poison");
    check(world.find(b)->effects.size() == 2, "sources incorrectly share effect instance");
    world.advance(1000);
    check(close(world.find(b)->hp, 980), "refresh postponed pending periodic tick");
    world.advance(1500);
    check(close(world.find(b)->hp, 970), "second source tick missing");

    // Refresh after the old final tick but before expiration must restart or
    // retain a future tick. A nonmultiple duration exposes a dormant tick clock.
    auto partialDuration = fixture();
    partialDuration.effects.at("poison").duration = 1500;
    game::World refreshed(std::move(partialDuration));
    const auto source = refreshed.spawn("hero", "arena", 1, {0, 0});
    const auto victim = refreshed.spawn("hero", "arena", 2, {10, 0});
    check(refreshed.cast(source, "poison", victim).ok, "partial-duration poison rejected");
    refreshed.advance(1200);
    check(close(refreshed.find(victim)->hp, 990), "initial partial-duration tick missing");
    check(refreshed.cast(source, "poison", victim).ok, "partial-duration refresh rejected");
    refreshed.advance(2200);
    check(close(refreshed.find(victim)->hp, 970), "refresh after final scheduled tick stopped periodic damage");
    refreshed.advance(2700);
    check(refreshed.find(victim)->effects.empty(), "refreshed partial-duration effect did not expire");
}
void modifiersShieldsAndDeath() {
    game::World world(fixture());
    const auto a = world.spawn("hero", "arena", 1, {0, 0});
    const auto b = world.spawn("hero", "arena", 2, {10, 0});
    check(world.cast(a, "buff", a).ok, "buff rejected");
    check(close(world.attribute(*world.find(a), "attack"), 40), "modifier not applied");
    check(world.cast(a, "buff", a).ok, "refresh rejected");
    check(close(world.attribute(*world.find(a), "attack"), 40), "refresh accumulated stat drift");
    check(world.cast(b, "shield", b).ok, "shield rejected");
    check(world.cast(a, "attack", b).ok, "shielded attack rejected");
    check(close(world.find(b)->hp, 1000), "shield failed to absorb");
    world.advance(500);
    check(world.cast(a, "attack", b).ok, "second attack rejected");
    check(close(world.find(b)->hp, 950), "shield pool did not deplete");
    world.advance(1500);
    check(close(world.attribute(*world.find(a), "attack"), 20), "expired buff left stat drift");
    check(world.cast(b, "heal", b).ok, "healing rejected");
    check(close(world.find(b)->hp, 1000), "healing did not clamp to max HP");

    auto lethal = fixture();
    lethal.effects.at("hit").magnitude = 10000;
    game::World fight(std::move(lethal));
    const auto x = fight.spawn("hero", "arena", 1, {0, 0});
    const auto y = fight.spawn("hero", "arena", 2, {10, 0});
    check(fight.cast(x, "attack", y).ok, "lethal hit rejected");
    check(!fight.find(y)->alive(), "lethal hit did not kill");
    check(!fight.cast(y, "heal", y).ok, "dead entity cast");
    check(!fight.move(y, {10, 0}).ok, "dead entity moved");
    fight.despawn(y);
    check(fight.spawn("hero", "arena", 2, {10, 0}) != y, "stale ID reused");
}
void extensionAndCapacity() {
    auto c = fixture();
    auto& heal = c.effects.at("heal");
    heal.handler = "regeneration";
    heal.magnitude = 3; heal.scalingAttribute = "luck"; heal.scalingFactor = 1;
    heal.duration = 2000; heal.period = 1000;
    c.characters.at("hero").attributes["luck"] = 7;
    auto registry = game::builtinEffects();
    registry.add("regeneration", {{}, [](game::EffectContext& context) {
        context.world.heal(context.instance.source, context.target,
            context.instance.power, context.definition.id);
    }, {}, [](const game::EffectDefinition& effect) {
        if (!effect.period || !effect.duration) throw std::invalid_argument("regeneration needs a timer");
    }});
    game::World world(std::move(c), std::move(registry));
    const auto a = world.spawn("hero", "arena", 1, {0, 0});
    const auto b = world.spawn("hero", "arena", 2, {10, 0});
    check(world.cast(b, "attack", a).ok && world.cast(a, "heal", a).ok, "extension setup failed");
    world.advance(2000);
    check(close(world.find(a)->hp, 990), "custom handler/new attribute did not compose");

    auto crowded = fixture();
    auto& skill = crowded.skills.at("stun");
    skill.effects.clear();
    for (int i = 0; i < 129; ++i) {
        auto effect = crowded.effects.at("stun");
        effect.id = "control_" + std::to_string(i);
        skill.effects.push_back(effect.id);
        crowded.effects.emplace(effect.id, std::move(effect));
    }
    game::World capacity(std::move(crowded));
    const auto x = capacity.spawn("hero", "arena", 1, {0, 0});
    const auto y = capacity.spawn("hero", "arena", 2, {10, 0});
    check(!capacity.cast(x, "stun", y).ok, "effect capacity not enforced");
    check(close(capacity.find(x)->mana, 200) && capacity.find(y)->effects.empty(), "rejected skill partially applied");
}
void eventBudgetRejection() {
    auto c = fixture();
    c.characters.at("hero").attributes["hp"] = 1.0e9;
    auto& skill = c.skills.at("attack");
    skill.effects.clear();
    skill.radius = 100;
    skill.maxTargets = 33;
    for (int i = 0; i < 256; ++i) {
        auto effect = c.effects.at("hit");
        effect.id = "burst_" + std::to_string(i);
        effect.magnitude = 1;
        effect.scalingAttribute.clear();
        effect.scalingFactor = 0;
        skill.effects.push_back(effect.id);
        c.effects.emplace(effect.id, std::move(effect));
    }
    game::World burst(std::move(c));
    const auto caster = burst.spawn("hero", "arena", 1, {0, 0});
    std::vector<game::EntityId> victims;
    for (int i = 0; i < 33; ++i) victims.push_back(burst.spawn("hero", "arena", 2, {10, 0}));
    burst.takeEvents();
    const auto rejected = burst.cast(caster, "attack", victims.front());
    check(!rejected.ok && rejected.message == "event_budget", "oversized cast did not reject its event budget");
    check(close(burst.find(caster)->mana, 200) && burst.find(caster)->cooldowns.empty(), "event-budget rejection charged cast");
    for (const auto victim : victims)
        check(close(burst.find(victim)->hp, 1.0e9) && burst.find(victim)->effects.empty(), "event-budget rejection partially damaged a target");
    check(burst.takeEvents().empty(), "rejected burst emitted partial events");
    for (std::size_t i = 1; i < victims.size(); ++i) burst.despawn(victims[i]);
    burst.takeEvents();
    check(burst.cast(caster, "attack", victims.front()).ok, "single-target burst rejected after reducing target count");
    check(close(burst.find(victims.front())->hp, 1.0e9 - 256), "accepted burst damage incorrect");

    auto timed = fixture();
    timed.characters.at("hero").attributes["hp"] = 1.0e9;
    timed.effects.at("poison").magnitude = 1;
    timed.effects.at("poison").duration = 60000;
    timed.effects.at("poison").period = 50;
    game::World ticks(std::move(timed));
    const auto source = ticks.spawn("hero", "arena", 1, {0, 0});
    std::vector<game::EntityId> poisoned;
    for (int i = 0; i < 15; ++i) {
        const auto target = ticks.spawn("hero", "arena", 2, {10, 0});
        check(ticks.cast(source, "poison", target).ok, "event-budget timer setup failed");
        poisoned.push_back(target);
    }
    ticks.takeEvents();
    bool advanceRejected = false;
    try { ticks.advance(60000); }
    catch (const std::invalid_argument& error) {
        advanceRejected = std::string(error.what()).find("event_budget") != std::string::npos;
    }
    check(advanceRejected && ticks.now() == 0, "oversized advance changed clock before rejection");
    for (const auto target : poisoned)
        check(close(ticks.find(target)->hp, 1.0e9) && ticks.find(target)->effects.front().nextTick == 50,
            "rejected advance changed resources or effect clock");
    check(ticks.takeEvents().empty(), "rejected advance emitted partial events");
    ticks.advance(1000);
    check(close(ticks.find(poisoned.front())->hp, 1.0e9 - 20), "smaller bounded advance failed");

    game::World queued(fixture());
    const auto player = queued.spawn("hero", "arena", 1, {0, 0});
    const auto enemy = queued.spawn("hero", "arena", 2, {10, 0});
    queued.takeEvents();
    queued.advance(1000);
    for (int i = 0; i < 16384; ++i)
        check(queued.move(player, {0, 0}).ok, "event queue fixture filled earlier than expected");
    const auto noRoom = queued.cast(player, "attack", enemy);
    check(!noRoom.ok && noRoom.message == "event_budget", "cast ignored already queued events");
    check(close(queued.find(player)->mana, 200) && close(queued.find(enemy)->hp, 1000), "full-queue cast changed resources");
    queued.advance(1100); // No effects: advancing without emitting events remains legal.
    const auto noMove = queued.move(player, {1, 0});
    check(!noMove.ok && noMove.message == "event_budget" && close(queued.find(player)->position.x, 0) &&
        queued.find(player)->lastMoveAt == 1000, "full-queue move changed position or movement credit");
    bool spawnRejected = false;
    try { queued.spawn("hero", "arena", 1, {0, 0}); }
    catch (const std::invalid_argument&) { spawnRejected = true; }
    check(spawnRejected && queued.entities().size() == 2, "full-queue spawn partially inserted an entity");
    check(queued.takeEvents().size() == 16384, "budget rejections emitted extra events");
    check(queued.spawn("hero", "arena", 1, {0, 0}) == enemy + 1, "rejected spawn consumed entity ID");
    check(queued.move(player, {10, 0}).ok, "rejected move consumed movement credit");
    check(queued.cast(player, "attack", enemy).ok, "rejected cast consumed cooldown");
}
void callbackFailureIsFatal() {
    auto content = fixture();
    content.effects.at("hit").handler = "failing_apply";
    auto registry = game::builtinEffects();
    registry.add("failing_apply", {[](game::EffectContext& context) {
        context.world.damage(context.instance.source, context.target, 1, context.definition.id);
        throw std::invalid_argument("intentional callback failure after mutation");
    }, {}, {}, {}});
    game::World world(std::move(content), std::move(registry));
    const auto caster = world.spawn("hero", "arena", 1, {0, 0});
    const auto target = world.spawn("hero", "arena", 2, {10, 0});
    bool fatal = false;
    try { world.cast(caster, "attack", target); }
    catch (const std::invalid_argument&) {
        throw std::runtime_error("callback fault escaped as a recoverable client argument error");
    } catch (const std::runtime_error& error) {
        fatal = std::string(error.what()).find("effect_handler_failed: hit.apply") != std::string::npos;
    }
    check(fatal, "failing callback did not propagate a fatal runtime error");
    check(close(world.find(target)->hp, 999), "callback fault fixture did not exercise partial mutation");
}
} // namespace

void runWorldTests() {
    rejectionAndMovement();
    tickingAndStacking();
    modifiersShieldsAndDeath();
    extensionAndCapacity();
    eventBudgetRejection();
    callbackFailureIsFatal();
}
