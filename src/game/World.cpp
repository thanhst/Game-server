#include "game/World.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace game {
namespace {
constexpr std::size_t maxEntities = 4096;
constexpr std::size_t maxEffects = 128;
constexpr std::size_t maxEvents = 16384;
constexpr double maxAttribute = 1.0e12;

Milliseconds after(Milliseconds time, Milliseconds duration)
{
    if (duration > (std::numeric_limits<Milliseconds>::max)() - time)
        throw std::overflow_error("Simulation clock exhausted");
    return time + duration;
}
double distance(Position a, Position b) { return std::hypot(a.x - b.x, a.y - b.y); }
bool inMap(Position p, const MapDefinition& map)
{
    return std::isfinite(p.x) && std::isfinite(p.y) &&
        p.x >= 0 && p.y >= 0 && p.x <= map.width && p.y <= map.height;
}
void timed(const EffectDefinition& effect)
{
    if (!effect.duration) throw std::invalid_argument("Effect requires duration: " + effect.id);
}
void instant(const EffectDefinition& effect)
{
    if (effect.duration || effect.period)
        throw std::invalid_argument("Effect must be instant: " + effect.id);
}
void nonnegative(const EffectDefinition& effect)
{
    if (effect.magnitude < 0 || effect.scalingFactor < 0)
        throw std::invalid_argument("Effect requires nonnegative power: " + effect.id);
}
bool reserveEvents(std::size_t& remaining, std::size_t count, std::uint64_t repetitions = 1)
{
    if (count && repetitions > remaining / count) return false;
    remaining -= static_cast<std::size_t>(count * repetitions);
    return true;
}
bool reserveCallback(std::size_t& remaining, const EffectHandler& handler,
    bool present, std::size_t effectSlots, std::uint64_t repetitions = 1)
{
    // A resource call can emit one absorption per active shield, damage/healing,
    // and death. Empty and resource-free callbacks emit no resource events.
    return !present || reserveEvents(remaining,
        (effectSlots + 2) * handler.maxResourceCallsPerCallback, repetitions);
}
void invokeCallback(const std::function<void(EffectContext&)>& callback,
    EffectContext& context, const char* phase)
{
    if (!callback) return;
    try { callback(context); }
    catch (const std::exception& error) {
        // Invalid arguments from callbacks are programmer faults, not rejected
        // client commands. Do not let a protocol catch resume a partially applied cast.
        throw std::runtime_error("effect_handler_failed: " + context.definition.id +
            "." + phase + ": " + error.what());
    } catch (...) {
        throw std::runtime_error("effect_handler_failed: " + context.definition.id +
            "." + phase + ": unknown exception");
    }
}
} // namespace

void EffectRegistry::add(std::string id, EffectHandler handler)
{
    if (id.empty() || (!handler.apply && !handler.tick))
        throw std::invalid_argument("Effect handler needs an ID and behavior");
    if (handler.maxResourceCallsPerCallback > 256)
        throw std::invalid_argument("Effect callback resource-call bound exceeds 256");
    if (!handlers_.emplace(std::move(id), std::move(handler)).second)
        throw std::invalid_argument("Duplicate effect handler");
}
const EffectHandler& EffectRegistry::at(const std::string& id) const
{
    const auto it = handlers_.find(id);
    if (it == handlers_.end()) throw std::invalid_argument("Unknown effect handler: " + id);
    return it->second;
}
void EffectRegistry::validate(const Content& content) const
{
    for (const auto& entry : content.effects) {
        const auto& handler = at(entry.second.handler);
        if (handler.validate) handler.validate(entry.second);
        if (entry.second.period && !handler.tick)
            throw std::invalid_argument("Periodic effect has no tick handler: " + entry.first);
    }
}
EffectRegistry builtinEffects()
{
    EffectRegistry registry;
    const auto hit = [](EffectContext& c) {
        c.world.damage(c.instance.source, c.target,
            c.instance.power * c.instance.stacks, c.definition.id);
    };
    registry.add("damage", { hit, {}, {}, [](const EffectDefinition& e) { instant(e); nonnegative(e); } });
    registry.add("heal", { [](EffectContext& c) {
        c.world.heal(c.instance.source, c.target, c.instance.power, c.definition.id);
    }, {}, {}, [](const EffectDefinition& e) { instant(e); nonnegative(e); } });
    registry.add("periodic_damage", { {}, hit, {}, [](const EffectDefinition& e) {
        timed(e); nonnegative(e);
        if (e.period < 50 || e.period > e.duration)
            throw std::invalid_argument("Periodic damage needs 50 <= period <= duration: " + e.id);
    } });
    registry.add("modifier", { [](EffectContext& c) {
        c.instance.modifiers[c.definition.attribute] = c.instance.power * c.instance.stacks;
    }, {}, {}, [](const EffectDefinition& e) {
        timed(e);
        if (e.attribute.empty() || e.period)
            throw std::invalid_argument("Modifier needs an attribute and no period: " + e.id);
    }, 0 });
    registry.add("control", { [](EffectContext& c) { c.instance.tags = c.definition.tags; }, {}, {},
        [](const EffectDefinition& e) {
            timed(e);
            if (e.tags.empty() || e.period)
                throw std::invalid_argument("Control needs tags and no period: " + e.id);
        }, 0 });
    registry.add("shield", { [](EffectContext& c) {
        c.instance.shield = (std::max)(0.0, c.instance.power * c.instance.stacks);
    }, {}, {}, [](const EffectDefinition& e) {
        timed(e); nonnegative(e);
        if (e.period) throw std::invalid_argument("Shield cannot tick: " + e.id);
    }, 0 });
    return registry;
}

World::World(Content content, EffectRegistry registry)
    : content_(std::move(content)), registry_(std::move(registry))
{
    content_.validate();
    registry_.validate(content_);
}
EntityId World::spawn(const std::string& archetype, const std::string& map,
    std::uint32_t team, Position position, std::string name)
{
    const auto definition = content_.characters.find(archetype);
    const auto location = content_.maps.find(map);
    if (definition == content_.characters.end() || location == content_.maps.end())
        throw std::invalid_argument("Unknown character or map");
    if (!inMap(position, location->second)) throw std::invalid_argument("Spawn outside map");
    if (entities_.size() >= maxEntities) throw std::runtime_error("World entity limit reached");
    if (nextEntity_ == (std::numeric_limits<EntityId>::max)())
        throw std::overflow_error("Entity IDs exhausted");
    if (events_.size() >= maxEvents)
        throw std::invalid_argument("event_budget: drain events before spawning");
    const auto& d = definition->second;
    Entity entity;
    entity.id = nextEntity_++;
    entity.archetype = archetype;
    entity.name = name.empty() ? d.name : std::move(name);
    entity.map = map;
    entity.team = team;
    entity.position = position;
    entity.baseAttributes = d.attributes;
    entity.tags = d.tags;
    entity.skills.insert(d.skills.begin(), d.skills.end());
    entity.hp = attribute(entity, "hp");
    entity.mana = attribute(entity, "mana");
    entity.lastMoveAt = now_;
    const auto id = entity.id;
    entities_.emplace(id, std::move(entity));
    emit({ "spawned", id, id, archetype, 0 });
    return id;
}
bool World::despawn(EntityId id)
{
    const auto it = entities_.find(id);
    if (it == entities_.end()) return false;
    emit({ "despawned", id, id, it->second.map, 0 });
    entities_.erase(it);
    return true;
}
const Entity* World::find(EntityId id) const
{
    const auto it = entities_.find(id);
    return it == entities_.end() ? nullptr : &it->second;
}
double World::attribute(const Entity& entity, const std::string& id) const
{
    const auto base = entity.baseAttributes.find(id);
    double value = base == entity.baseAttributes.end() ? 0.0 : base->second;
    for (const auto& effect : entity.effects) {
        const auto modifier = effect.modifiers.find(id);
        if (modifier != effect.modifiers.end()) value += modifier->second;
    }
    if (!std::isfinite(value)) throw std::runtime_error("Nonfinite effective attribute: " + id);
    return std::clamp(value, id == "hp" ? 1.0 : 0.0, maxAttribute);
}
bool World::hasTag(const Entity& entity, const std::string& tag) const
{
    if (entity.tags.count(tag)) return true;
    return std::any_of(entity.effects.begin(), entity.effects.end(), [&](const ActiveEffect& e) {
        return e.tags.count(tag) != 0;
    });
}
Result World::move(EntityId id, Position destination)
{
    const auto it = entities_.find(id);
    if (it == entities_.end()) return { false, "entity_missing" };
    auto& entity = it->second;
    if (!entity.alive()) return { false, "dead" };
    if (hasTag(entity, "stunned") || hasTag(entity, "rooted")) return { false, "movement_blocked" };
    if (!inMap(destination, content_.maps.at(entity.map))) return { false, "outside_map" };
    // Successful movement consumes the elapsed credit. Repeated packets in the
    // same tick cannot multiply speed; idle credit is capped at one second.
    const auto elapsed = (std::min)(now_ - entity.lastMoveAt, Milliseconds{1000});
    const double allowed = attribute(entity, "speed") * static_cast<double>(elapsed) / 1000.0;
    if (distance(entity.position, destination) > allowed) return { false, "movement_too_fast" };
    if (events_.size() >= maxEvents) return { false, "event_budget" };
    entity.position = destination;
    entity.lastMoveAt = now_;
    emit({ "moved", id, id, entity.map, 0 });
    return { true, "moved" };
}
Result World::cast(EntityId casterId, const std::string& skillId, EntityId targetId)
{
    const auto owner = entities_.find(casterId);
    if (owner == entities_.end()) return { false, "entity_missing" };
    auto& caster = owner->second;
    if (!caster.alive()) return { false, "dead" };
    if (hasTag(caster, "stunned") || hasTag(caster, "silenced")) return { false, "cast_blocked" };
    if (!caster.skills.count(skillId)) return { false, "skill_not_learned" };
    const auto& skill = content_.skills.at(skillId);
    const auto cooldown = caster.cooldowns.find(skillId);
    if (cooldown != caster.cooldowns.end() && cooldown->second > now_) return { false, "cooldown" };
    const double cost = skill.manaPercent ? attribute(caster, "mana") * skill.manaCost / 100.0 : skill.manaCost;
    if (caster.mana < cost) return { false, "insufficient_mana" };
    if (skill.target == TargetRule::Self) {
        if (targetId && targetId != casterId) return { false, "self_target_required" };
        targetId = casterId;
    }
    const auto target = entities_.find(targetId);
    if (target == entities_.end()) return { false, "target_missing" };
    const auto eligible = [&](const Entity& e) {
        return e.alive() && e.map == caster.map &&
            (skill.target == TargetRule::Self ? e.id == caster.id :
                skill.target == TargetRule::Enemy ? e.team != caster.team : e.team == caster.team);
    };
    if (!eligible(target->second)) return { false, "invalid_target" };
    if (distance(caster.position, target->second.position) > skill.range) return { false, "out_of_range" };
    std::vector<EntityId> targets{targetId};
    if (skill.radius > 0 && skill.maxTargets > 1 && skill.target != TargetRule::Self) {
        std::vector<std::pair<double, EntityId>> nearby;
        for (const auto& entry : entities_) {
            if (entry.first == targetId || !eligible(entry.second)) continue;
            const auto d = distance(entry.second.position, target->second.position);
            if (d <= skill.radius) nearby.emplace_back(d, entry.first);
        }
        std::sort(nearby.begin(), nearby.end()); // Stable tie break by entity ID.
        for (const auto& entry : nearby) {
            if (targets.size() == skill.maxTargets) break;
            targets.push_back(entry.second);
        }
    }
    // Reserve a conservative event bound for the entire operation before cost or
    // mutation. The host cannot drain the queue in the middle of one cast.
    std::size_t eventBudget = maxEvents - events_.size();
    if (!reserveEvents(eventBudget, 1)) return { false, "event_budget" }; // cast
    // Preflight every target before paying cost; a capacity rejection is atomic.
    for (const auto id : targets) {
        const auto& effects = entities_.at(id).effects;
        std::set<std::string> newEffects;
        for (const auto& effectId : skill.effects) {
            const auto& effect = content_.effects.at(effectId);
            after(now_, effect.duration);
            after(now_, effect.period);
            if (effect.duration && std::none_of(effects.begin(), effects.end(), [&](const ActiveEffect& e) {
                return e.definition == effectId && e.source == casterId;
            })) newEffects.insert(effectId);
        }
        if (effects.size() + newEffects.size() > maxEffects) return { false, "effect_limit" };
        const auto effectSlots = effects.size() + newEffects.size();
        for (const auto& effectId : skill.effects) {
            const auto& definition = content_.effects.at(effectId);
            const auto& handler = registry_.at(definition.handler);
            if (!reserveEvents(eventBudget, definition.visual.empty() ? 1 : 2) ||
                !reserveCallback(eventBudget, handler, static_cast<bool>(handler.apply), effectSlots))
                return { false, "event_budget" };
            if (definition.duration && definition.stacking == Stacking::Replace &&
                std::any_of(effects.begin(), effects.end(), [&](const ActiveEffect& effect) {
                    return effect.definition == effectId && effect.source == casterId;
                }) && !reserveCallback(eventBudget, handler, static_cast<bool>(handler.expire), effectSlots))
                return { false, "event_budget" };
        }
        // Any apply callback can kill the target. Include cleanup of all effects
        // that could then exist, with each expire callback's resource-event bound.
        const auto reserveExpiry = [&](const std::string& definitionId) {
            const auto& handler = registry_.at(content_.effects.at(definitionId).handler);
            return reserveEvents(eventBudget, 1) &&
                reserveCallback(eventBudget, handler, static_cast<bool>(handler.expire), effectSlots);
        };
        for (const auto& effect : effects)
            if (!reserveExpiry(effect.definition)) return { false, "event_budget" };
        for (const auto& effectId : newEffects)
            if (!reserveExpiry(effectId)) return { false, "event_budget" };
    }
    const auto readyAt = after(now_, skill.cooldown);
    caster.mana -= cost;
    caster.cooldowns[skillId] = readyAt;
    emit({ "cast", casterId, targetId, skillId, cost });
    // Ordered action composition is deliberate: e.g. hit, then poison, then stun.
    for (const auto id : targets) {
        auto& victim = entities_.at(id);
        for (const auto& effectId : skill.effects) {
            if (!victim.alive()) break;
            applyEffect(caster, victim, content_.effects.at(effectId));
        }
    }
    for (const auto id : targets) expireEffects(entities_.at(id));
    return { true, "cast" };
}
void World::applyEffect(Entity& caster, Entity& target, const EffectDefinition& definition)
{
    const auto& handler = registry_.at(definition.handler);
    ActiveEffect instance;
    if (nextEffect_ == (std::numeric_limits<std::uint64_t>::max)())
        throw std::overflow_error("Effect IDs exhausted");
    instance.instance = nextEffect_++;
    instance.definition = definition.id;
    instance.source = caster.id;
    instance.power = definition.magnitude + (definition.scalingAttribute.empty() ? 0.0 :
        attribute(caster, definition.scalingAttribute) * definition.scalingFactor);
    if (!std::isfinite(instance.power)) throw std::runtime_error("Nonfinite effect power");
    instance.expiresAt = after(now_, definition.duration);
    instance.nextTick = definition.period ? after(now_, definition.period) : 0;
    instance.tags = definition.tags;
    auto existing = std::find_if(target.effects.begin(), target.effects.end(), [&](const ActiveEffect& e) {
        return e.definition == definition.id && e.source == caster.id;
    });
    if (definition.duration && existing != target.effects.end()) {
        if (definition.stacking == Stacking::Stack)
            instance.stacks = (std::min)(existing->stacks + 1, definition.maxStacks);
        if (definition.stacking != Stacking::Replace) {
            instance.instance = existing->instance;
            // Refresh/stack never postpones the pending periodic tick.
            instance.nextTick = existing->nextTick;
        } else if (handler.expire) {
            EffectContext old{*this, target, *existing, definition};
            invokeCallback(handler.expire, old, "expire");
        }
    }
    EffectContext context{*this, target, instance, definition};
    invokeCallback(handler.apply, context, "apply");
    if (definition.duration) {
        if (existing == target.effects.end()) target.effects.push_back(std::move(instance));
        else *existing = std::move(instance);
    }
    clampResources(target);
    emit({ "effect_applied", caster.id, target.id, definition.id, 0 });
    if (!definition.visual.empty()) emit({ "visual", caster.id, target.id, definition.visual, 0 });
}
void World::advance(Milliseconds time)
{
    if (time < now_) throw std::invalid_argument("Simulation time cannot go backwards");
    if (time - now_ > 60000) throw std::invalid_argument("Advance at most 60000 ms per call");
    std::size_t eventBudget = maxEvents - events_.size();
    const auto rejectBudget = [] {
        throw std::invalid_argument("event_budget: drain events and use a smaller advance interval");
    };
    for (const auto& entry : entities_) {
        const auto& effects = entry.second.effects;
        bool hasWork = false;
        for (const auto& effect : effects) {
            hasWork = hasWork || effect.expiresAt <= time;
            const auto lastTick = (std::min)(time, effect.expiresAt);
            if (!effect.nextTick || effect.nextTick > lastTick) continue;
            hasWork = true;
            const auto& definition = content_.effects.at(effect.definition);
            const auto& handler = registry_.at(definition.handler);
            const auto ticks = 1 + (lastTick - effect.nextTick) / definition.period;
            if (!reserveCallback(eventBudget, handler, static_cast<bool>(handler.tick), effects.size(), ticks))
                rejectBudget();
        }
        // A tick or expiry callback may cause death and expire every other effect
        // on that entity. Reserve that cleanup even when nominal expiry is later.
        if (hasWork) for (const auto& effect : effects) {
            const auto& handler = registry_.at(content_.effects.at(effect.definition).handler);
            if (!reserveEvents(eventBudget, 1) ||
                !reserveCallback(eventBudget, handler, static_cast<bool>(handler.expire), effects.size()))
                rejectBudget();
        }
    }
    // Process due times in order so a large step gives the same periodic damage
    // and modifier expiry as small steps. All final ticks precede same-time expiry.
    for (;;) {
        Milliseconds due = (std::numeric_limits<Milliseconds>::max)();
        for (const auto& entry : entities_) for (const auto& effect : entry.second.effects) {
            due = (std::min)(due, effect.expiresAt);
            if (effect.nextTick) due = (std::min)(due, effect.nextTick);
        }
        if (due > time || due == (std::numeric_limits<Milliseconds>::max)()) break;
        now_ = due;
        for (auto& entry : entities_) {
            auto& entity = entry.second;
            for (auto& effect : entity.effects) {
                if (!entity.alive()) break;
                if (!effect.nextTick || effect.nextTick != due || due > effect.expiresAt) continue;
                const auto& definition = content_.effects.at(effect.definition);
                const auto& handler = registry_.at(definition.handler);
                EffectContext context{*this, entity, effect, definition};
                invokeCallback(handler.tick, context, "tick");
                // Keep the cadence even beyond the current expiry: a refresh
                // between the last tick and expiry must be able to extend it.
                effect.nextTick = definition.period <= (std::numeric_limits<Milliseconds>::max)() - due ?
                    after(due, definition.period) : 0;
            }
        }
        for (auto& entry : entities_) expireEffects(entry.second);
    }
    now_ = time;
}
void World::expireEffects(Entity& entity)
{
    auto it = entity.effects.begin();
    while (it != entity.effects.end()) {
        if (entity.alive() && it->expiresAt > now_) { ++it; continue; }
        const auto& definition = content_.effects.at(it->definition);
        const auto& handler = registry_.at(definition.handler);
        EffectContext context{*this, entity, *it, definition};
        invokeCallback(handler.expire, context, "expire");
        emit({ "effect_expired", it->source, entity.id, it->definition, 0 });
        it = entity.effects.erase(it);
    }
    clampResources(entity);
}
void World::clampResources(Entity& entity) const
{
    entity.hp = std::clamp(entity.hp, 0.0, attribute(entity, "hp"));
    entity.mana = std::clamp(entity.mana, 0.0, attribute(entity, "mana"));
}
void World::damage(EntityId source, Entity& target, double amount, const std::string& effect)
{
    if (!std::isfinite(amount) || amount < 0) throw std::invalid_argument("Invalid damage amount");
    if (!target.alive() || amount == 0) return;
    double remaining = (std::max)(1.0, amount - attribute(target, "defense"));
    for (auto& active : target.effects) {
        const auto absorbed = (std::min)(remaining, active.shield);
        active.shield -= absorbed;
        remaining -= absorbed;
        if (absorbed > 0) emit({ "absorbed", source, target.id, active.definition, absorbed });
        if (remaining <= 0) break;
    }
    const auto applied = (std::min)(target.hp, remaining);
    target.hp -= applied;
    emit({ "damage", source, target.id, effect, applied });
    if (!target.alive()) emit({ "died", source, target.id, effect, 0 });
}
void World::heal(EntityId source, Entity& target, double amount, const std::string& effect)
{
    if (!std::isfinite(amount) || amount < 0) throw std::invalid_argument("Invalid healing amount");
    if (!target.alive()) return; // Revival is a separate mechanic, not implicit healing.
    const auto applied = (std::min)(amount, attribute(target, "hp") - target.hp);
    target.hp += applied;
    emit({ "healed", source, target.id, effect, applied });
}
void World::emit(GameEvent event)
{
    if (events_.size() >= maxEvents) throw std::runtime_error("Undrained world event queue overflow");
    events_.push_back(std::move(event));
}
std::vector<GameEvent> World::takeEvents()
{
    auto events = std::move(events_);
    events_.clear();
    return events;
}
} // namespace game
