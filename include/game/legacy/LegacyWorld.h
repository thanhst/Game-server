#pragma once

#include "game/legacy/CombatRules.h"
#include "game/legacy/LegacyContent.h"
#include <memory>
#include <utility>

namespace game::legacy {
struct LegacyActorState {
    BattleTarget battle; // kind == Player; derived target fields stay actor-owned too.
    std::int32_t classId = 0;
    std::map<std::int32_t, std::int32_t> learned; // template ID -> learned point
    std::int32_t selectedSkillId = -1;
    std::map<std::int32_t, SkillRuntime> skillRuntimes;
};
struct LegacySummonState {
    BattleTarget battle;
    std::int64_t owner = 0;
    std::int32_t damagePercent = 0;
};
struct WorldOperation {
    CombatOutcome outcome = CombatOutcome::Rejected;
    std::string reason;
    explicit operator bool() const noexcept { return outcome == CombatOutcome::Applied; }
};
struct LegacyScheduledEffect {
    std::uint64_t token = 0;
    std::int64_t source = 0;
    EffectRecipe recipe;
    BattleTargetKind targetKind = BattleTargetKind::Player;
    bool targetIsSummon = false;
    std::int64_t startsAt = 0, expiresAt = 0;
    bool started = false;
};

// Single owner thread, one map/zone, immutable definitions, separate player/mob IDs.
class LegacyWorld {
public:
    LegacyWorld(std::shared_ptr<const ContentSnapshot> content, std::int32_t mapId, RandomDraw random);
    void addActor(LegacyActorState actor); // Throws on invalid/unlearned definition references.
    void addMob(BattleTarget mob);
    bool removeActor(std::int64_t id);
    const LegacyActorState* actor(std::int64_t id) const;
    const BattleTarget* mob(std::int64_t id) const;
    const LegacySummonState* summon(std::int64_t owner) const;
    const std::map<std::int64_t, LegacyActorState>& actors() const noexcept { return actors_; }
    const std::map<std::int64_t, BattleTarget>& mobs() const noexcept { return mobs_; }
    std::int64_t nowMs() const noexcept { return nowMs_; }
    std::int32_t mapId() const noexcept { return map_.id; }

    WorldOperation selectSkill(std::int64_t actor, std::int32_t skillId);
    AttackResult attackMob(std::int64_t actor, std::int64_t targetMob, bool summonedTarget = false,
                           bool summonOwnerTargetAllowed = false);
    AttackResult attackPlayer(std::int64_t actor, std::int64_t targetPlayer, bool pvpAllowed);
    AttackResult useNonFocus(std::int64_t actor, std::int32_t actionType);
    WorldOperation advance(std::int64_t nowMs);
    std::vector<CombatEvent> takeEvents();
    const std::vector<LegacyScheduledEffect>& scheduledEffects() const noexcept { return effects_; }

    // Required for transform: resolve final stats through the full source pipeline.
    // Called with battle.monkey already changed. Return all updated battle values.
    using StatResolver = std::function<BattleTarget(const LegacyActorState&)>;
    void setStatResolver(StatResolver resolver) { statResolver_ = std::move(resolver); }
    void setRuleProfile(RuleProfile profile) { rules_ = std::move(profile); }
    using EffectRule = std::function<WorldOperation(BattleTarget&, const EffectRecipe&, bool start)>;
    void registerEffectRule(std::string kind, EffectRule rule);
    // Collision grids/dimensions are absent from the source-only SQL snapshot.
    // A source-backed map movement adapter must handle terrain/flight/waypoints.
    using MovementRule = std::function<std::pair<WorldOperation, BattleTarget>(const LegacyActorState&, BattlePosition, bool)>;
    void setMovementRule(MovementRule rule) { movementRule_ = std::move(rule); }
    WorldOperation move(std::int64_t actor, BattlePosition requested, bool flight);

private:
    AttackContext context(std::int64_t actor) const;
    AttackResult failure(std::int64_t actor, CombatOutcome, const std::string&) const;
    AttackResult evaluateAndCommit(AttackContext, const std::string& ruleKey, bool summonedTarget = false);
    void preflight(const AttackContext&, const AttackResult&) const;
    void schedule(std::int64_t source, const AttackResult&, bool summonedTarget);
    void beginEffect(LegacyScheduledEffect&);
    void endEffect(const LegacyScheduledEffect&, bool emitEvent = true);
    BattleTarget* effectTarget(const LegacyScheduledEffect&);
    void applySummonFollowup(const AttackContext&, AttackResult&) const;
    void clearActorEffects(std::int64_t actor);

    std::shared_ptr<const ContentSnapshot> content_;
    BattleMap map_;
    RandomDraw random_;
    RuleProfile rules_;
    std::map<std::int64_t, LegacyActorState> actors_;
    std::map<std::int64_t, BattleTarget> mobs_;
    std::map<std::int64_t, LegacySummonState> summons_;
    std::vector<LegacyScheduledEffect> effects_;
    std::vector<CombatEvent> events_;
    std::int64_t nowMs_ = 0;
    std::uint64_t nextEffect_ = 1;
    StatResolver statResolver_;
    MovementRule movementRule_;
    std::map<std::string, EffectRule> effectRules_;
};
} // namespace game::legacy
