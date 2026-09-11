#pragma once

#include "game/Content.h"
#include <functional>
#include <string_view>

namespace game {
struct ActiveEffect {
    std::uint64_t instance = 0;
    std::string definition;
    EntityId source = 0;
    double power = 0;
    std::uint32_t stacks = 1;
    Milliseconds expiresAt = 0;
    Milliseconds nextTick = 0;
    Attributes modifiers;
    std::set<std::string> tags;
    double shield = 0;
};
struct Entity {
    EntityId id = 0;
    std::string archetype;
    std::string name;
    std::string map;
    std::uint32_t team = 0;
    Position position;
    Attributes baseAttributes;
    std::set<std::string> tags;
    std::set<std::string> skills;
    double hp = 0;
    double mana = 0;
    Milliseconds lastMoveAt = 0;
    std::map<std::string, Milliseconds> cooldowns;
    std::vector<ActiveEffect> effects;
    bool alive() const { return hp > 0; }
};
struct GameEvent {
    std::string kind;
    EntityId source = 0;
    EntityId target = 0;
    std::string contentId;
    double value = 0;
};
struct Result {
    bool ok = false;
    std::string message;
    explicit operator bool() const { return ok; }
};

class World;
struct EffectContext {
    World& world;
    Entity& target;
    ActiveEffect& instance;
    const EffectDefinition& definition;
};
struct EffectHandler {
    // Callbacks run on the world owner thread. They may change target resources
    // and instance state, but must not spawn/despawn/cast or edit effect vectors
    // or scheduling fields. Resources/shields must remain finite and nonnegative.
    // Each callback may call World::damage/heal no more than the declared bound.
    // Violating this contract or throwing is a handler programming error; world
    // operations do not roll back arbitrary user callback mutations.
    std::function<void(EffectContext&)> apply;
    std::function<void(EffectContext&)> tick;
    std::function<void(EffectContext&)> expire;
    std::function<void(const EffectDefinition&)> validate;
    std::uint32_t maxResourceCallsPerCallback = 1;
};
class EffectRegistry {
public:
    void add(std::string id, EffectHandler handler);
    const EffectHandler& at(const std::string& id) const;
    void validate(const Content& content) const;
private:
    std::map<std::string, EffectHandler> handlers_;
};
EffectRegistry builtinEffects();

// One owner thread per world. Transport callbacks never mutate entities.
// Stable IDs cross system boundaries; no session owns an Entity pointer.
class World {
public:
    explicit World(Content content, EffectRegistry registry = builtinEffects());
    EntityId spawn(const std::string& archetype, const std::string& map,
        std::uint32_t team, Position position, std::string name = {});
    bool despawn(EntityId id);
    const Entity* find(EntityId id) const;
    const std::map<EntityId, Entity>& entities() const { return entities_; }
    const Content& content() const { return content_; }
    Milliseconds now() const { return now_; }
    Result move(EntityId id, Position destination);
    Result cast(EntityId caster, const std::string& skill, EntityId target);
    // Absolute simulation clock, monotonically advanced by the host's fixed tick.
    void advance(Milliseconds now);
    std::vector<GameEvent> takeEvents();
    double attribute(const Entity& entity, const std::string& id) const;
    bool hasTag(const Entity& entity, const std::string& tag) const;
    void damage(EntityId source, Entity& target, double amount, const std::string& effect);
    void heal(EntityId source, Entity& target, double amount, const std::string& effect);
private:
    void applyEffect(Entity& caster, Entity& target, const EffectDefinition& effect);
    void expireEffects(Entity& entity);
    void clampResources(Entity& entity) const;
    void emit(GameEvent event);
    Content content_;
    EffectRegistry registry_;
    std::map<EntityId, Entity> entities_;
    std::vector<GameEvent> events_;
    EntityId nextEntity_ = 1;
    std::uint64_t nextEffect_ = 1;
    Milliseconds now_ = 0;
};
} // namespace game
