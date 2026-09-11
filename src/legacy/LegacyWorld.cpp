#include "game/legacy/LegacyWorld.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace game::legacy {
namespace {
using I = std::int64_t;
constexpr std::size_t EntityLimit = 4096, EffectLimit = 32768, EventLimit = 65536;
struct OperationFailure { WorldOperation result; };
WorldOperation ok() { return {CombatOutcome::Applied, "applied"}; }
WorldOperation no(CombatOutcome outcome, std::string reason) { return {outcome, std::move(reason)}; }
[[noreturn]] void unsupported(const std::string& reason) { throw OperationFailure{no(CombatOutcome::Unsupported, reason)}; }
I checkedAdd(I a, I b) {
    if ((b > 0 && a > (std::numeric_limits<I>::max)() - b) ||
        (b < 0 && a < (std::numeric_limits<I>::min)() - b)) unsupported("integer_overflow");
    return a + b;
}
BattleMap sourceMap(int id) {
    BattleMap m; m.id = id;
    m.pet = id == 155; m.barrack = id >= 53 && id <= 62; m.cold = id >= 105 && id <= 110;
    m.nappa = (id >= 63 && id <= 77) || (id >= 79 && id <= 83);
    m.future = id == 102 || id == 92 || id == 93 || id == 94 || (id >= 96 && id <= 100) || id == 103;
    m.baseBabidi = id == 114 || id == 115 || id == 117 || id == 119 || id == 120;
    return m;
}
bool selfRecipe(const std::string& kind) {
    return kind == "energy_protection" || kind == "summon" || kind == "recovery_start" ||
        kind == "transform_after_alive_check" || kind == "charge_clear" || kind == "special_clear";
}
bool builtinRecipe(const std::string& kind) {
    return selfRecipe(kind) || kind == "sleep" || kind == "blind" || kind == "chocolate" ||
        kind == "hold" || kind == "energy_shield_expire";
}
bool summonSkill(SkillId id) {
    return id == SkillId::CHIEU_DAM_DEMON || id == SkillId::CHIEU_MASENKO || id == SkillId::LIEN_HOAN ||
        id == SkillId::CHIEU_DAM_DRAGON || id == SkillId::CHIEU_KAMEJOKO || id == SkillId::KAIOKEN ||
        id == SkillId::CHIEU_DAM_GALICK || id == SkillId::CHIEU_ANTOMIC;
}
void validState(const BattleTarget& state) {
    if (state.id < 0 || state.maxHp <= 0 || state.maxMana < 0 || state.hp < 0 || state.hp > state.maxHp ||
        state.mana > state.maxMana || state.damageFull < 0 || state.position.x < -32768 || state.position.x > 32767 ||
        state.position.y < -32768 || state.position.y > 32767)
        throw std::invalid_argument("Invalid legacy actor/mob state");
}
} // namespace

LegacyWorld::LegacyWorld(std::shared_ptr<const ContentSnapshot> content, int mapId, RandomDraw random)
    : content_(std::move(content)), map_(sourceMap(mapId)), random_(std::move(random)), rules_(hunr2026Rules()) {
    if (!content_ || content_->maps().count(mapId) == 0) throw std::invalid_argument("Legacy world requires a loaded map");
    if (!random_) throw std::invalid_argument("Legacy world requires an explicit random source");
}
void LegacyWorld::addActor(LegacyActorState state) {
    validState(state.battle);
    if (state.battle.kind != BattleTargetKind::Player) throw std::invalid_argument("Actor must be a player target kind");
    if (actors_.size() + mobs_.size() >= EntityLimit) throw std::length_error("Legacy entity limit");
    for (const auto& learned : state.learned) content_->skill(state.classId, learned.first).level(learned.second);
    if (state.selectedSkillId != -1 && state.learned.count(state.selectedSkillId) == 0)
        throw std::invalid_argument("Selected legacy skill is not learned");
    for (const auto& runtime : state.skillRuntimes) {
        const auto learned = state.learned.find(runtime.first);
        if (learned == state.learned.end()) throw std::invalid_argument("Runtime for unlearned legacy skill");
        runtime.second.remainingCooldownMs(content_->skill(state.classId, runtime.first).level(learned->second), nowMs_);
    }
    if (!actors_.emplace(state.battle.id, std::move(state)).second) throw std::invalid_argument("Duplicate legacy actor ID");
}
void LegacyWorld::addMob(BattleTarget state) {
    validState(state);
    if (state.kind != BattleTargetKind::Mob || state.mobMe || content_->mobs().count(state.templateId) == 0)
        throw std::invalid_argument("Mob requires a loaded normal mob template");
    if (actors_.size() + mobs_.size() >= EntityLimit) throw std::length_error("Legacy entity limit");
    if (!mobs_.emplace(state.id, std::move(state)).second) throw std::invalid_argument("Duplicate legacy mob ID");
}
const LegacyActorState* LegacyWorld::actor(I id) const {
    const auto it = actors_.find(id); return it == actors_.end() ? nullptr : &it->second;
}
const BattleTarget* LegacyWorld::mob(I id) const {
    const auto it = mobs_.find(id); return it == mobs_.end() ? nullptr : &it->second;
}
const LegacySummonState* LegacyWorld::summon(I owner) const {
    const auto it = summons_.find(owner); return it == summons_.end() ? nullptr : &it->second;
}
void LegacyWorld::clearActorEffects(I id) {
    for (auto it = effects_.begin(); it != effects_.end();) {
        if ((it->recipe.kind == "hold" && it->source == id) ||
            (it->targetKind == BattleTargetKind::Player && it->recipe.target == id) ||
            (it->targetIsSummon && it->recipe.target == id)) {
            if (it->started) endEffect(*it);
            it = effects_.erase(it);
        } else ++it;
    }
}
bool LegacyWorld::removeActor(I id) {
    if (!actors_.count(id)) return false;
    // Disconnect cleanup does not invoke a stat resolver on a departing actor.
    actors_.erase(id);
    clearActorEffects(id);
    summons_.erase(id);
    return true;
}
WorldOperation LegacyWorld::selectSkill(I id, int skillId) {
    auto it = actors_.find(id);
    if (it == actors_.end()) return no(CombatOutcome::Rejected, "actor_missing");
    if (!it->second.learned.count(skillId)) return no(CombatOutcome::Rejected, "skill_not_learned");
    it->second.selectedSkillId = skillId;
    return ok();
}
AttackContext LegacyWorld::context(I id) const {
    const auto& owner = actors_.at(id);
    const auto point = owner.learned.at(owner.selectedSkillId);
    AttackContext c;
    c.actor = owner.battle; c.target = owner.battle; c.skill = content_->skill(owner.classId, owner.selectedSkillId);
    c.level = c.skill.level(point);
    const auto runtime = owner.skillRuntimes.find(owner.selectedSkillId);
    if (runtime != owner.skillRuntimes.end()) c.runtime = runtime->second;
    c.nowMs = nowMs_; c.random = random_; c.map = map_; c.completeZoneMobs = true;
    I total = 0;
    for (const auto& actor : actors_) if (actor.second.battle.human) total = checkedAdd(total, actor.second.battle.hp);
    for (const auto& mob : mobs_) { total = checkedAdd(total, mob.second.hp); c.zoneMobs.push_back(mob.second); }
    c.totalZoneHp = total;
    return c;
}
AttackResult LegacyWorld::failure(I id, CombatOutcome outcome, const std::string& reason) const {
    AttackResult r; r.outcome = outcome; r.reason = reason;
    if (const auto* a = actor(id)) { r.actor = a->battle; r.target = a->battle; }
    return r;
}
AttackResult LegacyWorld::attackMob(I source, I target, bool summonedTarget, bool summonOwnerTargetAllowed) {
    if (summonedTarget && (source == target || !summonOwnerTargetAllowed))
        return failure(source, CombatOutcome::Rejected, "summon_owner_target_not_allowed");
    try {
        auto c = context(source);
        if (summonedTarget) {
            const auto* s = summon(target);
            if (!s) return failure(source, CombatOutcome::Rejected, "summon_missing");
            c.target = s->battle;
        } else {
            const auto* m = mob(target);
            if (!m) return failure(source, CombatOutcome::Rejected, "mob_missing");
            c.target = *m;
        }
        return evaluateAndCommit(std::move(c), "legacy.mob", summonedTarget);
    } catch (const std::out_of_range&) { return failure(source, CombatOutcome::Rejected, "actor_or_selected_skill_missing"); }
    catch (const OperationFailure& e) { return failure(source, e.result.outcome, e.result.reason); }
}
AttackResult LegacyWorld::attackPlayer(I source, I target, bool pvpAllowed) {
    if (source == target) return failure(source, CombatOutcome::Rejected, "self_player_target");
    try {
        auto c = context(source);
        const auto* p = actor(target);
        if (!p) return failure(source, CombatOutcome::Rejected, "target_player_missing");
        c.target = p->battle; c.playerTargetAllowed = pvpAllowed;
        return evaluateAndCommit(std::move(c), "legacy.player");
    } catch (const std::out_of_range&) { return failure(source, CombatOutcome::Rejected, "actor_or_selected_skill_missing"); }
    catch (const OperationFailure& e) { return failure(source, e.result.outcome, e.result.reason); }
}
AttackResult LegacyWorld::useNonFocus(I source, int actionType) {
    try {
        auto c = context(source); c.nonFocusType = actionType;
        return evaluateAndCommit(std::move(c), "legacy.nonfocus");
    } catch (const std::out_of_range&) { return failure(source, CombatOutcome::Rejected, "actor_or_selected_skill_missing"); }
    catch (const OperationFailure& e) { return failure(source, e.result.outcome, e.result.reason); }
}
void LegacyWorld::preflight(const AttackContext&, const AttackResult& result) const {
    if (events_.size() + result.events.size() + result.effects.size() * 4 + 8 > EventLimit)
        unsupported("event_capacity");
    if (effects_.size() + result.effects.size() > EffectLimit) unsupported("effect_capacity");
    for (const auto& recipe : result.effects) {
        if (!builtinRecipe(recipe.kind) && !effectRules_.count(recipe.kind)) unsupported("effect_recipe_not_installed:" + recipe.kind);
        if (recipe.delayMs < 0 || recipe.durationMs < 0) unsupported("negative_effect_time");
        checkedAdd(checkedAdd(nowMs_, recipe.delayMs), recipe.durationMs);
        if (recipe.kind == "transform_after_alive_check" && !statResolver_) unsupported("transform_stat_resolver_missing");
        if (recipe.kind == "summon") {
            if (!content_->mobs().count(recipe.templateId)) unsupported("summon_template_missing");
            if (recipe.magnitude <= 0) unsupported("invalid_summon_hp");
            if (result.actor.position.y - 40 < -32768) unsupported("summon_position_outside_java_short_domain");
        }
    }
}
void LegacyWorld::applySummonFollowup(const AttackContext& c, AttackResult& r) const {
    const auto own = summons_.find(c.actor.id);
    if (own == summons_.end() || !summonSkill(c.skill.id) || r.target.dead || r.target.hp <= 0) return;
    if (r.target.kind == BattleTargetKind::Player &&
        (r.target.damageLimit != -1 || static_cast<double>(r.target.hp) * 100.0 / r.target.maxHp < 5.0)) return;
    if (r.target.kind == BattleTargetKind::Mob && r.target.templateId == 70) return;
    I damage = percentOf(r.actor.damageFull, own->second.damagePercent);
    if (r.actor.setPikkoroDaimao) damage = checkedAdd(damage, damage);
    if (r.target.kind == BattleTargetKind::Player) {
        if (r.target.protectedByEnergy && damage >= r.target.hp) damage = r.target.hp - 1;
        if (damage <= 0) damage = -1; // Base Player.injure(null, mob, damage).
    } else if (r.target.templateId == 0) damage = 10;
    r.target.hp = checkedAdd(r.target.hp, -damage);
    r.events.push_back({"summon_damage", r.target.id, damage});
    if (r.target.hp <= 0) {
        r.target.dead = true;
        if (r.target.kind == BattleTargetKind::Mob) r.target.status = 0;
        r.events.push_back({r.target.kind == BattleTargetKind::Mob ? "mob_death" : "player_death", r.target.id, 0});
    }
}
AttackResult LegacyWorld::evaluateAndCommit(AttackContext c, const std::string& key, bool summonedTarget) {
    auto result = rules_.evaluate(key, c);
    if (result.outcome != CombatOutcome::Applied) return result;
    try {
        if (key != "legacy.nonfocus") applySummonFollowup(c, result);
        preflight(c, result);
    } catch (const OperationFailure& e) { auto r = failure(c.actor.id, e.result.outcome, e.result.reason); r.target = c.target; return r; }
    catch (const std::exception& e) { auto r = failure(c.actor.id, CombatOutcome::Unsupported, e.what()); r.target = c.target; return r; }
    // Transaction includes immediate recipes and custom handlers; callbacks must be pure.
    const auto oldActors = actors_; const auto oldMobs = mobs_; const auto oldSummons = summons_;
    const auto oldEffects = effects_; const auto oldEvents = events_; const auto oldNext = nextEffect_;
    try {
        auto& owner = actors_.at(c.actor.id);
        static_cast<BattleActor&>(owner.battle) = result.actor;
        owner.skillRuntimes[owner.selectedSkillId] = result.runtime;
        for (const auto& mob : result.zoneMobs) if (mobs_.count(mob.id)) mobs_.at(mob.id) = mob;
        if (key != "legacy.nonfocus") {
            if (result.target.kind == BattleTargetKind::Player) actors_.at(result.target.id).battle = result.target;
            else if (summonedTarget) summons_.at(result.target.id).battle = result.target;
            else mobs_.at(result.target.id) = result.target;
        }
        events_.insert(events_.end(), result.events.begin(), result.events.end());
        schedule(c.actor.id, result, summonedTarget);
        // Return authoritative values after immediate recipe application too.
        result.actor = actors_.at(c.actor.id).battle;
        if (key == "legacy.nonfocus") result.target = actors_.at(c.actor.id).battle;
        else if (result.target.kind == BattleTargetKind::Player) result.target = actors_.at(result.target.id).battle;
        else if (summonedTarget) result.target = summons_.at(result.target.id).battle;
        else result.target = mobs_.at(result.target.id);
        return result;
    } catch (const std::exception& e) {
        actors_ = oldActors; mobs_ = oldMobs; summons_ = oldSummons; effects_ = oldEffects; events_ = oldEvents; nextEffect_ = oldNext;
        auto r = failure(c.actor.id, CombatOutcome::Unsupported, e.what()); r.target = c.target; return r;
    } catch (const OperationFailure& e) {
        actors_ = oldActors; mobs_ = oldMobs; summons_ = oldSummons; effects_ = oldEffects; events_ = oldEvents; nextEffect_ = oldNext;
        auto r = failure(c.actor.id, e.result.outcome, e.result.reason); r.target = c.target; return r;
    }
}
BattleTarget* LegacyWorld::effectTarget(const LegacyScheduledEffect& e) {
    if (e.targetKind == BattleTargetKind::Player) {
        const auto it = actors_.find(e.recipe.target); return it == actors_.end() ? nullptr : &it->second.battle;
    }
    if (e.targetIsSummon) { const auto it = summons_.find(e.recipe.target); return it == summons_.end() ? nullptr : &it->second.battle; }
    const auto it = mobs_.find(e.recipe.target); return it == mobs_.end() ? nullptr : &it->second;
}
void LegacyWorld::schedule(I source, const AttackResult& result, bool summonedTarget) {
    for (const auto& recipe : result.effects) {
        const auto kind = selfRecipe(recipe.kind) ? BattleTargetKind::Player : result.target.kind;
        const bool isSummon = !selfRecipe(recipe.kind) && summonedTarget;
        for (auto it = effects_.begin(); it != effects_.end();) {
            if (it->recipe.kind == recipe.kind && it->recipe.target == recipe.target && it->targetKind == kind && it->targetIsSummon == isSummon) {
                if (it->started) endEffect(*it);
                it = effects_.erase(it);
            } else ++it;
        }
        if (nextEffect_ == (std::numeric_limits<std::uint64_t>::max)()) unsupported("effect_id_exhausted");
        LegacyScheduledEffect effect;
        effect.token = nextEffect_++; effect.source = source; effect.recipe = recipe;
        effect.targetKind = kind; effect.targetIsSummon = isSummon;
        effect.startsAt = checkedAdd(nowMs_, recipe.delayMs); effect.expiresAt = checkedAdd(effect.startsAt, recipe.durationMs);
        if (!recipe.delayMs && recipe.kind != "energy_shield_expire") {
            beginEffect(effect); effect.started = true;
        }
        if (!effect.started || recipe.durationMs > 0) effects_.push_back(std::move(effect));
    }
}
void LegacyWorld::beginEffect(LegacyScheduledEffect& e) {
    auto* target = effectTarget(e);
    if (!target) return;
    const auto& k = e.recipe.kind;
    if (k == "sleep") target->sleeping = true;
    else if (k == "blind") target->blind = true;
    else if (k == "chocolate") { target->chocolate = true; target->damageReduction = static_cast<int>(e.recipe.magnitude); }
    else if (k == "energy_protection") target->protectedByEnergy = true;
    else if (k == "energy_shield_expire") target->protectedByEnergy = false;
    else if (k == "hold") {
        target->held = true; target->heldAsDetainee = true;
        if (auto it = actors_.find(e.source); it != actors_.end()) it->second.battle.held = true;
    } else if (k == "charge_clear") target->charging = false;
    else if (k == "special_clear") target->skillSpecial = false;
    else if (k == "recovery_start") target->recovering = true;
    else if (k == "summon") {
        LegacySummonState summon; summon.owner = e.source;
        auto& b = summon.battle;
        b.id = e.source; b.kind = BattleTargetKind::Mob; b.mobMe = true; b.human = false;
        b.position = {target->position.x, target->position.y - 40}; b.templateId = e.recipe.templateId;
        b.hp = b.maxHp = e.recipe.magnitude; b.status = 4;
        const auto& owner = actors_.at(e.source);
        summon.damagePercent = content_->skill(owner.classId, owner.selectedSkillId).level(owner.learned.at(owner.selectedSkillId)).damage;
        summons_[e.source] = std::move(summon);
    } else if (k == "transform_after_alive_check") {
        if (target->dead || target->hp <= 0) return;
        auto& actor = actors_.at(e.source); actor.battle.monkey = true;
        actor.battle = statResolver_(actor); actor.battle.monkey = true;
        if (actor.battle.id != e.source || actor.battle.kind != BattleTargetKind::Player) unsupported("stat_resolver_changed_identity");
        validState(actor.battle);
        actor.battle.hp = (std::min)(actor.battle.maxHp, checkedAdd(actor.battle.hp, percentOf(actor.battle.maxHp, 50)));
        actor.battle.mana = (std::min)(actor.battle.maxMana, checkedAdd(actor.battle.mana, percentOf(actor.battle.maxMana, 50)));
    } else {
        const auto result = effectRules_.at(k)(*target, e.recipe, true);
        if (!result) throw OperationFailure{result};
    }
    events_.push_back({"effect_started:" + k, e.recipe.target, e.recipe.magnitude});
}
void LegacyWorld::endEffect(const LegacyScheduledEffect& e) {
    auto* target = effectTarget(e);
    const auto& k = e.recipe.kind;
    if (k == "summon") summons_.erase(e.source);
    if (target) {
        if (k == "sleep") target->sleeping = false;
        else if (k == "blind") target->blind = false;
        else if (k == "chocolate") { target->chocolate = false; target->damageReduction = 0; }
        else if (k == "energy_protection") target->protectedByEnergy = false;
        else if (k == "hold") { target->held = false; target->heldAsDetainee = false; }
        else if (k == "recovery_start") target->recovering = false;
        else if (k == "transform_after_alive_check" && target->monkey) {
            auto& actor = actors_.at(e.source); actor.battle.monkey = false;
            actor.battle = statResolver_(actor); actor.battle.monkey = false;
            if (actor.battle.id != e.source || actor.battle.kind != BattleTargetKind::Player) unsupported("stat_resolver_changed_identity");
            validState(actor.battle);
        } else if (!builtinRecipe(k)) {
            const auto result = effectRules_.at(k)(*target, e.recipe, false);
            if (!result) throw OperationFailure{result};
        }
    }
    if (k == "hold") { const auto owner = actors_.find(e.source); if (owner != actors_.end()) owner->second.battle.held = false; }
    events_.push_back({"effect_expired:" + k, e.recipe.target, 0});
}
WorldOperation LegacyWorld::advance(I time) {
    if (time < nowMs_) return no(CombatOutcome::Rejected, "clock_went_backwards");
    const auto oldActors = actors_; const auto oldMobs = mobs_; const auto oldSummons = summons_;
    const auto oldEffects = effects_; const auto oldEvents = events_; const auto oldTime = nowMs_;
    try {
        for (;;) {
            auto next = effects_.end();
            I due = (std::numeric_limits<I>::max)();
            for (auto it = effects_.begin(); it != effects_.end(); ++it) {
                const auto at = it->started ? it->expiresAt : it->startsAt;
                if (at <= time && (next == effects_.end() || at < due || (at == due && it->token < next->token))) { next = it; due = at; }
            }
            if (next == effects_.end()) break;
            nowMs_ = due;
            if (!next->started) {
                beginEffect(*next); next->started = true;
                if (!next->recipe.durationMs) effects_.erase(next);
            } else { endEffect(*next); effects_.erase(next); }
            if (events_.size() > EventLimit) unsupported("event_capacity");
        }
        nowMs_ = time;
        return ok();
    } catch (const std::exception& e) {
        actors_ = oldActors; mobs_ = oldMobs; summons_ = oldSummons; effects_ = oldEffects; events_ = oldEvents; nowMs_ = oldTime;
        return no(CombatOutcome::Unsupported, e.what());
    } catch (const OperationFailure& e) {
        actors_ = oldActors; mobs_ = oldMobs; summons_ = oldSummons; effects_ = oldEffects; events_ = oldEvents; nowMs_ = oldTime;
        return e.result;
    }
}
std::vector<CombatEvent> LegacyWorld::takeEvents() { auto result = std::move(events_); events_.clear(); return result; }
void LegacyWorld::registerEffectRule(std::string kind, EffectRule rule) {
    if (kind.empty() || !rule || builtinRecipe(kind)) throw std::invalid_argument("Custom effect requires a new kind and callback");
    if (!effectRules_.emplace(std::move(kind), std::move(rule)).second) throw std::invalid_argument("Duplicate effect kind");
}
WorldOperation LegacyWorld::move(I id, BattlePosition requested, bool flight) {
    const auto it = actors_.find(id);
    if (it == actors_.end()) return no(CombatOutcome::Rejected, "actor_missing");
    const auto& a = it->second.battle;
    if (a.dead || a.blind || a.frozen || a.sleeping || a.charging || a.stone || (a.held && a.heldAsDetainee))
        return no(CombatOutcome::Rejected, "movement_blocked");
    if (!movementRule_) return no(CombatOutcome::Unsupported, "source_collision_movement_adapter_missing");
    if (events_.size() + effects_.size() + 1 > EventLimit) return no(CombatOutcome::Unsupported, "event_capacity");
    try {
        const auto result = movementRule_(it->second, requested, flight);
        if (!result.first) return result.first;
        validState(result.second);
        if (result.second.id != id || result.second.kind != BattleTargetKind::Player)
            return no(CombatOutcome::Unsupported, "movement_adapter_changed_identity");
        if (summons_.count(id) && result.second.position.y - 40 < -32768)
            return no(CombatOutcome::Unsupported, "summon_position_outside_java_short_domain");
        it->second.battle = result.second; it->second.battle.recovering = false;
        for (auto held = effects_.begin(); held != effects_.end();) {
            if (held->recipe.kind == "hold" && held->source == id) { endEffect(*held); held = effects_.erase(held); }
            else ++held;
        }
        if (auto summon = summons_.find(id); summon != summons_.end()) {
            summon->second.battle.position = {result.second.position.x, result.second.position.y - 40};
        }
        events_.push_back({"moved", id, flight ? 1 : 0});
        return ok();
    } catch (const std::exception& e) { return no(CombatOutcome::Unsupported, e.what()); }
}
} // namespace game::legacy
