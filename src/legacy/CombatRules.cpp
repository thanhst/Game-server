#include "game/legacy/CombatRules.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace game::legacy {
namespace {
using I = std::int64_t;
struct Stop { CombatOutcome outcome; std::string reason; };
[[noreturn]] void unsupported(const std::string& reason) { throw Stop{CombatOutcome::Unsupported, reason}; }
[[noreturn]] void reject(const std::string& reason) { throw Stop{CombatOutcome::Rejected, reason}; }
I add(I a, I b) {
    if ((b > 0 && a > (std::numeric_limits<I>::max)() - b) ||
        (b < 0 && a < (std::numeric_limits<I>::min)() - b)) unsupported("integer_overflow");
    return a + b;
}
I mul(I a, I b) {
    if (a < 0 || b < 0) unsupported("negative_multiplication_input");
    if (b && a > (std::numeric_limits<I>::max)() / b) unsupported("integer_overflow");
    return a * b;
}
I pct(I value, I percentage) {
    if (value < 0 || percentage < -(std::numeric_limits<std::int32_t>::max)() ||
        percentage > (std::numeric_limits<std::int32_t>::max)()) unsupported("unsupported_percentage_domain");
    const auto amount = percentOf(value, static_cast<std::int32_t>(percentage < 0 ? -percentage : percentage));
    return percentage < 0 ? -amount : amount;
}
I gain(I value, I percentage) { return add(value, pct(value, percentage)); }
I opt(const BattleActor& actor, std::int32_t id) {
    const auto it = actor.options.find(id);
    return it == actor.options.end() ? 0 : it->second;
}
I draw(const AttackContext& c, I min, I max) {
    if (min >= max) return max; // Utils.nextLong guard, not a random draw.
    if (!c.random) unsupported("random_source_missing");
    const auto value = c.random(min, max);
    if (value < min || value >= max) unsupported("random_draw_out_of_range");
    return value;
}
I distance(BattlePosition a, BattlePosition b) {
    const double dx = static_cast<double>(b.x) - a.x, dy = static_cast<double>(b.y) - a.y;
    return static_cast<I>(std::sqrt(dx * dx + dy * dy));
}
I range(const SkillLevel& level) { return distance({}, {level.dx, level.dy}); }
bool melee(SkillId id) {
    return id == SkillId::CHIEU_DAM_DRAGON || id == SkillId::CHIEU_DAM_DEMON ||
        id == SkillId::CHIEU_DAM_GALICK || id == SkillId::KAIOKEN || id == SkillId::LIEN_HOAN;
}
bool beam(SkillId id) {
    return id == SkillId::CHIEU_KAMEJOKO || id == SkillId::CHIEU_MASENKO || id == SkillId::CHIEU_ANTOMIC;
}
bool basic(SkillId id) { return melee(id) || beam(id); }
bool damageSkill(SkillId id) {
    return basic(id) || id == SkillId::MAKANKOSAPPO || id == SkillId::QUA_CAU_KENH_KHI ||
        id == SkillId::DICH_CHUYEN_TUC_THOI || id == SkillId::DANH || id == SkillId::CHUONG;
}
AttackResult original(const AttackContext& c) {
    AttackResult r;
    r.actor = c.actor; r.target = c.target; r.runtime = c.runtime; r.zoneMobs = c.zoneMobs;
    return r;
}
void event(AttackResult& r, std::string kind, I target, I value = 0) {
    r.events.push_back({std::move(kind), target, value});
}
void effect(AttackResult& r, std::string kind, I target, I seconds, I magnitude = 0, I delay = 0, int templateId = -1) {
    if (seconds < 0) unsupported("negative_effect_duration");
    r.effects.push_back({std::move(kind), target, mul(seconds, 1000), delay, magnitude, templateId});
}
void validateActor(const BattleActor& a) {
    if (!a.unsupportedMechanics.empty()) unsupported("unported_context:" + *a.unsupportedMechanics.begin());
    if (a.maxHp <= 0 || a.maxMana < 0 || (a.hp < 0 && !a.dead) || a.hp > a.maxHp || a.mana > a.maxMana ||
        a.damageFull < 0 || a.stamina < 0) unsupported("invalid_actor_snapshot");
    if (a.position.x < -32768 || a.position.x > 32767 || a.position.y < -32768 || a.position.y > 32767)
        unsupported("position_outside_java_short_domain");
}
void validate(const AttackContext& c, bool withTarget) {
    validateActor(c.actor);
    if (withTarget) validateActor(c.target);
    c.level.validate();
    skillName(c.skill.id);
    if (c.level.point > c.skill.maxPoint || c.nowMs < 0) unsupported("invalid_skill_or_clock_snapshot");
    c.runtime.effectiveCooldownMs(c.level);
    if ((c.runtime.lastUseMs && *c.runtime.lastUseMs < 0) || c.actor.lastXChuongMs < 0 ||
        c.actor.lastRecoveryMs < 0 || (withTarget && c.target.lastWakeUpMs < 0)) unsupported("negative_timestamp");
    if (c.actor.dead || c.actor.hp <= 0) reject("actor_dead");
    if (c.actor.frozen || c.actor.sleeping || c.actor.held || c.actor.stone) reject("actor_controlled");
}
void recover(BattleActor& a, I hp, I mana) {
    a.hp = (std::min)(a.maxHp, add(a.hp, hp));
    a.mana = (std::min)(a.maxMana, add(a.mana, mana));
}
void specialBonus(BattleActor& a, SkillId id, I& damage, bool& critical) {
    const auto s = a.specialId;
    if ((s == 1 && id == SkillId::CHIEU_DAM_GALICK) || (s == 2 && id == SkillId::CHIEU_ANTOMIC) ||
        (s == 3 && a.monkey) || (s == 11 && id == SkillId::CHIEU_DAM_DRAGON) ||
        (s == 12 && id == SkillId::CHIEU_KAMEJOKO) || (s == 21 && id == SkillId::CHIEU_DAM_DEMON) ||
        (s == 22 && id == SkillId::CHIEU_MASENKO) || (s == 26 && id == SkillId::LIEN_HOAN))
        damage = gain(damage, a.specialParam);
    if (s == 31 && mul(a.hp, 100) / a.maxHp < a.specialParam) critical = true;
}
void consumeBonus(BattleActor& a, SkillId id, I& damage) {
    if (basic(id) && a.percentDamageBonus > 0) {
        damage = gain(damage, a.percentDamageBonus);
        a.percentDamageBonus = 0;
    }
}
void commitCost(const AttackContext& c, AttackResult& r, I cost, bool mob) {
    if (!r.actor.skillSpecial) r.runtime.recordUse(c.nowMs);
    r.actor.mana = add(r.actor.mana, -cost);
    if (!r.actor.boss && !(mob && r.actor.enduranceCharm))
        r.actor.stamina = (std::max)(0, r.actor.stamina - 1);
    event(r, "skill_paint", r.target.id, c.level.id);
}
bool useSkill(const AttackContext& c, AttackResult& r) {
    r.actor.lastAttackMs = c.nowMs;
    if (c.skill.id == SkillId::TRI_THUONG) unsupported("healing_requires_complete_player_area_and_revival_adapter");
    if (c.skill.id == SkillId::KAIOKEN) {
        if (mul(r.actor.hp, 100) / r.actor.maxHp <= 10) {
            r.reason = "kaioken_hp_gate";
            return false;
        }
        r.actor.hp = add(r.actor.hp, -pct(r.actor.maxHp, 10));
    }
    if (c.skill.id == SkillId::TROI) {
        if (r.target.kind == BattleTargetKind::Mob && r.target.held) {
            r.reason = "target_already_held";
            return false;
        }
        r.runtime.recordUse(c.nowMs);
        r.actor.held = true; r.target.held = true; r.target.heldAsDetainee = true;
        effect(r, "hold", r.target.id, c.level.damage);
        if (r.actor.specialId == 7) r.actor.percentDamageBonus = r.actor.specialParam;
        r.actor.critFirstHit = true;
        // Player.useSkill recomputes this cost without the outer skillSpecial exemption.
        r.actor.mana = add(r.actor.mana, -manaCost(c.skill, c.level, r.actor.maxMana, r.actor.boss));
        r.reason = "held";
        return false;
    }
    return true;
}
// Returns true for the source switch's early return (no normal damage/cooldown tail).
bool crowdControl(const AttackContext& c, AttackResult& r, I cost, I& percentDamage, bool player) {
    const auto id = c.skill.id;
    const auto previous = c.runtime.lastUseMs.value_or(0);
    const bool flagSkill = c.nowMs - previous > c.runtime.effectiveCooldownMs(c.level);
    if (id == SkillId::THOI_MIEN) {
        if (!player || flagSkill) {
            if (player) r.runtime.recordUse(c.nowMs);
            r.actor.mana = add(r.actor.mana, -cost);
            r.target.sleeping = true;
            effect(r, "sleep", r.target.id, percentDamage);
            event(r, "skill_paint", r.target.id, c.level.id);
            if (r.actor.specialId == 17) r.actor.percentDamageBonus = r.actor.specialParam;
        }
        r.reason = "sleep_branch";
        return true;
    }
    if (id == SkillId::BIEN_SOCOLA) {
        if (player || !r.target.mobMe) {
            effect(r, "chocolate", r.target.id, 30, c.level.damage, 0, 4132);
            r.target.chocolate = true;
            r.target.damageReduction = c.level.damage;
            r.actor.mana = add(r.actor.mana, -cost);
            r.actor.critFirstHit = true;
            event(r, "skill_paint", r.target.id, c.level.id);
            if (r.actor.specialId == 27) r.actor.percentDamageBonus = r.actor.specialParam;
        }
        r.reason = "chocolate_branch";
        return true;
    }
    if (id == SkillId::DICH_CHUYEN_TUC_THOI && (!player || flagSkill)) {
        if (player) r.runtime.recordUse(c.nowMs);
        r.actor.position = r.target.position;
        r.target.blind = true;
        effect(r, "blind", r.target.id, 3);
        percentDamage = 200 + c.level.point * 10;
        r.actor.critFirstHit = true;
        if (r.actor.specialId == 16) r.actor.percentDamageBonus = r.actor.specialParam;
    }
    return false;
}
void spiritBomb(const AttackContext& c, AttackResult& r) {
    if (!c.totalZoneHp || !c.completeZoneMobs) unsupported("spirit_bomb_requires_complete_zone_snapshot");
    if (*c.totalZoneHp < 0) unsupported("negative_zone_hp");
    std::set<I> ids;
    for (const auto& mob : c.zoneMobs) {
        validateActor(mob);
        if (mob.kind != BattleTargetKind::Mob || !ids.insert(mob.id).second) unsupported("invalid_zone_mob_snapshot");
    }
    r.damage = add(*c.totalZoneHp / 10, mul(r.actor.damageFull, 10));
    if (r.target.kind == BattleTargetKind::Player && r.target.boss) r.damage /= 2;
    if (r.actor.setKirin) r.damage = mul(r.damage, 2);
}
void splash(const AttackContext& c, AttackResult& r) {
    for (auto& mob : r.zoneMobs) {
        if (r.target.kind == BattleTargetKind::Mob && mob.id == r.target.id) mob = r.target;
        if (mob.dead || mob.status == 0 || mob.hp <= 0 || distance(mob.position, r.target.position) >= range(c.level)) continue;
        mob.hp = add(mob.hp, -r.damage);
        event(r, "splash_damage", mob.id, r.damage);
        if (mob.hp <= 0) { mob.dead = true; mob.status = 0; event(r, "mob_death", mob.id); }
        if (r.target.kind == BattleTargetKind::Mob && mob.id == r.target.id) r.target = mob;
    }
}
void experience(const AttackContext& c, AttackResult& r) {
    if (r.target.mobMe) return;
    I exp = (std::max)(I{1}, r.damage / 10);
    if (c.map.future) { /* Empty source branch intentionally prevents later map branches. */ }
    else if (c.map.nappa) exp = gain(exp, -20);
    else if (c.map.cold) exp = gain(exp, 50);
    else if (c.map.id >= 168 && c.map.id <= 174) { }
    else if (c.map.barrack || c.map.id == 181 || c.map.id == 183 || c.map.id == 184) exp = 0;
    else if (exp > 10000000) exp = draw(c, 9000000, 10000001);
    if (r.actor.specialId == 30) exp = gain(exp, r.actor.specialParam);
    I percent = r.actor.flag > 0 ? (r.actor.flag == 8 ? 10 : 5) : 0;
    for (const auto option : {101, 155, 88, 83}) percent = add(percent, opt(r.actor, option));
    percent = add(percent, mul(r.actor.satellites343, 20));
    exp = (std::max)(I{1}, gain(exp, percent));
    event(r, "add_power_and_potential_input", r.actor.id, exp);
}
template <typename Function> AttackResult evaluate(const AttackContext& c, Function body) {
    try {
        auto r = original(c);
        body(r);
        r.outcome = CombatOutcome::Applied;
        if (r.reason.empty()) r.reason = "applied";
        return r;
    } catch (const Stop& stop) {
        auto r = original(c); r.outcome = stop.outcome; r.reason = stop.reason; return r;
    } catch (const std::overflow_error&) {
        auto r = original(c); r.outcome = CombatOutcome::Unsupported; r.reason = "integer_overflow"; return r;
    } catch (const std::invalid_argument& error) {
        auto r = original(c); r.outcome = CombatOutcome::Unsupported; r.reason = error.what(); return r;
    }
}
void commonAdmission(const AttackContext& c, AttackResult& r, bool player, I& cost) {
    validate(c, true);
    if (player != (c.target.kind == BattleTargetKind::Player)) reject("target_kind_mismatch");
    if (c.target.dead || c.target.hp <= 0 || (!player && c.target.status == 0)) reject("target_dead");
    if (!player && !c.target.mobMe && c.target.status != 4) reject("mob_not_attackable");
    if (player && !c.playerTargetAllowed && c.skill.id != SkillId::TRI_THUONG) reject("player_target_not_allowed");
    if (player && c.nowMs - c.target.lastWakeUpMs <= 2000) reject("wake_up_protection");
    if (!player && c.actor.immortalCharm && c.actor.hp == 1) reject("immortal_charm_attack_blocked");
    if (c.map.pet && !c.actor.disciple && c.skill.id != SkillId::TROI && c.skill.id != SkillId::TRI_THUONG)
        reject("pet_map_skill_gate");
    cost = c.actor.skillSpecial ? 0 : manaCost(c.skill, c.level, c.actor.maxMana, c.actor.boss);
    if (c.actor.mana < cost) reject("insufficient_mana");
    if (c.skill.type == 3) reject("use_alone_skill");
    if (!c.actor.skillSpecial && c.runtime.isCooldown(c.level, c.nowMs)) reject("cooldown");
    if (!damageSkill(c.skill.id) && c.skill.id != SkillId::TROI && c.skill.id != SkillId::THOI_MIEN &&
        c.skill.id != SkillId::BIEN_SOCOLA) unsupported("unported_attack_skill");
    r.actor.recovering = false;
}
} // namespace

AttackResult attackMob(const AttackContext& c) {
    return evaluate(c, [&](AttackResult& r) {
        I cost = 0;
        commonAdmission(c, r, false, cost);
        const auto originalDistance = distance(c.actor.position, c.target.position);
        I percentDamage = c.level.damage;
        r.miss = draw(c, 0, 10) == 0;
        r.critical = draw(c, 0, 100) < r.actor.criticalFull;
        if (r.actor.critFirstHit) { r.critical = true; r.actor.critFirstHit = false; }
        if (c.skill.id == SkillId::QUA_CAU_KENH_KHI) r.miss = false;
        if (crowdControl(c, r, cost, percentDamage, false)) return;
        r.damage = gain(r.actor.damageFull, percentDamage - 100);
        r.damage = draw(c, r.damage - r.damage / 10, r.damage);
        r.damage = gain(r.damage, opt(r.actor, 19));
        if (r.actor.powerfulCharm) r.damage = mul(r.damage, 2);
        if ((c.skill.id == SkillId::CHIEU_DAM_GALICK && r.actor.setKakarot) ||
            (c.skill.id == SkillId::CHIEU_KAMEJOKO && r.actor.setSongoku)) r.damage = mul(r.damage, 3);
        if (r.actor.disciple && r.actor.discipleCharm && !r.actor.discipleType2) r.damage = mul(r.damage, 2);
        specialBonus(r.actor, c.skill.id, r.damage, r.critical);
        if (r.actor.skillSpecial) r.critical = false;
        if (r.critical) r.damage = gain(mul(r.damage, 2), opt(r.actor, 5));
        consumeBonus(r.actor, c.skill.id, r.damage);
        if (c.skill.id == SkillId::MAKANKOSAPPO) {
            r.damage = pct(r.actor.mana, percentDamage);
            r.critical = false; r.miss = false; r.actor.mana = 1;
        } else if (c.skill.id == SkillId::QUA_CAU_KENH_KHI) spiritBomb(c, r);
        else if (c.skill.id == SkillId::LIEN_HOAN && r.actor.setOcTieu) r.damage = gain(r.damage, 100);
        if (opt(r.actor, 111) > draw(c, 0, 100)) r.miss = true;
        if (!r.actor.skillSpecial) {
            if (r.target.templateId == 0) r.damage = 10;
            else if (r.miss) r.damage = -1;
            if (originalDistance > range(c.level) + 50) r.damage = 0;
        }
        if (r.damage == 0) r.critical = false;
        if (r.damage >= r.target.hp)
            r.damage = r.target.hp == r.target.maxHp && !r.actor.skillSpecial ? r.target.hp - 1 : r.target.hp;
        if (r.target.templateId == 70)
            r.damage = r.actor.mapPhuHo == 126 ? (std::max)(I{2}, r.target.hp / 50) : (std::max)(I{1}, r.target.hp / 100);
        if (useSkill(c, r)) {
            commitCost(c, r, cost, true);
            if (r.damage > 0) {
                if (!r.actor.skillSpecial && !r.actor.heroicCharm && !r.actor.autoPlay && r.target.levelBoss != 0)
                    r.damage = (std::min)(r.damage, r.target.maxHp / 10);
                r.target.hp = add(r.target.hp, -r.damage);
                if (c.skill.id == SkillId::QUA_CAU_KENH_KHI) splash(c, r);
                recover(r.actor, pct(r.damage, add(opt(r.actor, 95), opt(r.actor, 104))), pct(r.damage, opt(r.actor, 96)));
            }
            experience(c, r);
            event(r, "mob_damage", r.target.id, r.damage);
            if (r.target.hp <= 0) { r.target.dead = true; r.target.status = 0; event(r, "mob_death", r.target.id); }
        }
        r.actor.skillSpecial = false;
    });
}

AttackResult attackPlayer(const AttackContext& c) {
    return evaluate(c, [&](AttackResult& r) {
        if (c.actor.boss || c.target.boss) unsupported("boss_player_subclass_damage_rules");
        if (c.actor.autoPlay) unsupported("pvp_autoplay_death_teleport_requires_zone_mob_selection");
        I cost = 0;
        commonAdmission(c, r, true, cost);
        const auto originalDistance = distance(c.actor.position, c.target.position);
        I percentDamage = c.level.damage;
        r.miss = draw(c, 0, 100) < r.target.percentMiss;
        r.critical = draw(c, 0, 100) < r.actor.criticalFull;
        if (r.actor.critFirstHit) { r.critical = true; r.actor.critFirstHit = false; }
        if (r.target.held && r.target.heldAsDetainee) r.critical = true;
        if (c.skill.id == SkillId::QUA_CAU_KENH_KHI) r.miss = false;
        if (crowdControl(c, r, cost, percentDamage, true)) return;
        r.damage = gain(r.actor.damageFull, percentDamage - 100);
        r.damage = draw(c, r.damage - r.damage / 10, r.damage);
        if (c.skill.id == SkillId::CHIEU_KAMEJOKO) r.damage = gain(r.damage, r.actor.optionKame);
        if (c.skill.id == SkillId::CHIEU_KAMEJOKO && r.actor.setSongoku) r.damage = gain(r.damage, 100);
        if (c.skill.id == SkillId::CHIEU_DAM_GALICK && r.actor.setKakarot) r.damage = gain(r.damage, 150);
        if (beam(c.skill.id) && opt(r.actor, 159) > 0 && c.nowMs - r.actor.lastXChuongMs >= 60000 && r.target.human) {
            r.damage = mul(r.damage, opt(r.actor, 159)); r.actor.lastXChuongMs = c.nowMs;
        }
        // Java computes xuyenGiap and consumes these draws, but never reads it later in attackPlayer.
        if (beam(c.skill.id) && opt(r.actor, 98) > 0) draw(c, 0, 100);
        if (melee(c.skill.id) && opt(r.actor, 99) > 0) draw(c, 0, 100);
        if (c.map.baseBabidi) r.damage = r.target.maxHp / 10;
        r.damage = (std::max)(I{1}, r.damage);
        if (beam(c.skill.id) && opt(r.target, 3) > 0) {
            recover(r.target, 0, pct(r.damage, opt(r.target, 3))); r.miss = true;
        }
        if (r.target.cellArmor) r.damage /= 2;
        if (opt(r.target, 157) > 0) {
            if (!r.target.maxMana) unsupported("zero_max_mana_in_low_mana_reduction");
            if (mul(r.target.mana, 100) / r.target.maxMana < 20) r.damage = gain(r.damage, -opt(r.target, 157));
        }
        specialBonus(r.actor, c.skill.id, r.damage, r.critical);
        consumeBonus(r.actor, c.skill.id, r.damage);
        if (c.skill.id == SkillId::MAKANKOSAPPO) {
            r.damage = gain(pct(r.actor.mana, percentDamage), r.actor.optionLaze);
            r.critical = false; r.miss = false; r.actor.mana = 1;
        } else if (c.skill.id == SkillId::QUA_CAU_KENH_KHI) spiritBomb(c, r);
        else if (c.skill.id == SkillId::LIEN_HOAN && r.actor.setOcTieu) r.damage = gain(r.damage, 100);
        if (opt(r.actor, 111) > draw(c, 0, 100) && !r.target.xinbatoResistance) r.miss = true;
        if (r.actor.skillSpecial) r.critical = false;
        if (r.critical && draw(c, 0, 100) < opt(r.target, 191)) r.critical = false;
        if (r.critical) r.damage = gain(mul(r.damage, 2), opt(r.actor, 5));
        if (r.target.damageLimit != -1) r.damage = (std::min)(r.damage, r.target.damageLimit);
        r.damage = gain(r.damage, -opt(r.target, 94));
        if (r.damage > 0) {
            I reflected = pct(r.damage, add(opt(r.target, 97), melee(c.skill.id) ? opt(r.actor, 15) : 0));
            if (!r.actor.canReflect) reflected = -1;
            if (reflected >= r.actor.hp) reflected = r.actor.hp - 1;
            if (reflected > 0) { r.actor.hp -= reflected; event(r, "reflected_damage", r.actor.id, reflected); }
        }
        const auto shieldBranch = [&] {
            if (!r.miss && r.target.protectedByEnergy) {
                if (r.damage > r.target.maxHp) effect(r, "energy_shield_expire", r.target.id, 0);
                r.damage = 1;
                if (r.target.hp <= r.damage) r.miss = true;
            }
        };
        shieldBranch();
        if (r.miss) r.damage = 0;
        if (r.damage == 0) r.critical = false;
        if (originalDistance > range(c.level) + 50) r.damage = 0;
        if (r.damage < 0) r.damage = 0;
        shieldBranch(); // Deliberate second source branch, after the range check.
        if (useSkill(c, r)) {
            commitCost(c, r, cost, false);
            if (r.damage > 0) {
                r.target.hp = add(r.target.hp, -r.damage);
                if (c.skill.id == SkillId::QUA_CAU_KENH_KHI) splash(c, r);
                recover(r.actor, pct(r.damage, opt(r.actor, 95)), pct(r.damage, opt(r.actor, 96)));
            }
            event(r, "player_damage", r.target.id, r.damage);
            if (r.target.hp <= 0) { r.target.dead = true; event(r, "player_death", r.target.id); }
        }
        r.actor.skillSpecial = false;
    });
}

AttackResult useNonFocusSkill(const AttackContext& c) {
    return evaluate(c, [&](AttackResult& r) {
        // Recovery updates bypass control gates in the Java wrapper while recovery is active.
        if (c.actor.recovering) {
            validateActor(c.actor); if (c.actor.dead) reject("actor_dead");
            if (c.nowMs < 0) unsupported("invalid_clock_snapshot");
        } else validate(c, false);
        if (c.nonFocusType == 3) { r.actor.recovering = false; event(r, "recovery_stopped", r.actor.id); return; }
        if (c.nonFocusType == 2) {
            if (r.actor.recovering) {
                if (r.actor.hp >= r.actor.maxHp && r.actor.mana >= r.actor.maxMana) r.actor.recovering = false;
                else if (c.nowMs - r.actor.lastRecoveryMs >= 1000) {
                    recover(r.actor, pct(r.actor.maxHp, c.level.damage), pct(r.actor.maxMana, c.level.damage));
                    r.actor.lastRecoveryMs = c.nowMs; r.actor.critFirstHit = true;
                }
                if (r.actor.hp >= r.actor.maxHp && r.actor.mana >= r.actor.maxMana) r.actor.recovering = false;
                event(r, "recovery_update", r.actor.id);
            }
            return;
        }
        if (c.skill.type != 3 && c.skill.id != SkillId::QUA_CAU_KENH_KHI && c.skill.id != SkillId::MAKANKOSAPPO)
            reject("not_nonfocus_skill");
        if (c.runtime.isCooldown(c.level, c.nowMs)) reject("cooldown");
        const I cost = manaCost(c.skill, c.level, r.actor.maxMana, r.actor.boss);
        if (r.actor.mana < cost) reject("insufficient_mana");
        r.actor.recovering = false;
        if (c.nonFocusType == 1 && c.skill.id == SkillId::TAI_TAO_NANG_LUONG) {
            r.actor.recovering = true; r.actor.lastRecoveryStartedMs = c.nowMs;
            effect(r, "recovery_start", r.actor.id, r.actor.human ? 10 : 2, c.level.damage);
        } else if (c.nonFocusType == 4 && (c.skill.id == SkillId::QUA_CAU_KENH_KHI || c.skill.id == SkillId::MAKANKOSAPPO)) {
            r.actor.mana = add(r.actor.mana, -cost); r.actor.charging = true; r.actor.skillSpecial = true;
            effect(r, "charge_clear", r.actor.id, 0, 0, 3000);
            effect(r, "special_clear", r.actor.id, 0, 0, 4000);
        } else if (c.nonFocusType == 6 && c.skill.id == SkillId::BIEN_HINH) {
            r.actor.charging = true;
            effect(r, "transform_after_alive_check", r.actor.id, transformDurationSeconds(c.level.point, r.actor.setCaDic),
                   c.level.point, 3000);
            effect(r, "charge_clear", r.actor.id, 0, 0, 3000);
        } else if (c.nonFocusType == 8 && c.skill.id == SkillId::DE_TRUNG) {
            r.actor.mana = add(r.actor.mana, -cost);
            effect(r, "summon", r.actor.id, summonDurationSeconds(c.level.point),
                   summonHp(r.actor.maxHp, c.level.point), 0, summonTemplateId(c.level.point));
            event(r, "summon_damage_percent", r.actor.id, c.level.damage);
        } else if (c.nonFocusType == 9 && c.skill.id == SkillId::KHIEN_NANG_LUONG) {
            r.actor.mana = add(r.actor.mana, -cost);
            effect(r, "energy_protection", r.actor.id, shieldDurationSeconds(c.level.point));
        } else unsupported("nonfocus_action_requires_unported_area_or_actor_system");
        r.runtime.recordUse(c.nowMs);
        event(r, "nonfocus_skill", r.actor.id, c.nonFocusType);
    });
}

void RuleProfile::registerRule(std::string key, Rule rule) {
    if (key.empty() || !rule) throw std::invalid_argument("Combat rule requires key and callback");
    if (!rules_.emplace(std::move(key), std::move(rule)).second) throw std::invalid_argument("Duplicate combat rule key");
}
AttackResult RuleProfile::evaluate(const std::string& key, const AttackContext& context) const {
    const auto it = rules_.find(key);
    if (it == rules_.end()) { auto r = original(context); r.outcome = CombatOutcome::Unsupported; r.reason = "rule_not_registered:" + key; return r; }
    return it->second(context);
}
RuleProfile hunr2026Rules() {
    RuleProfile profile;
    profile.registerRule("legacy.mob", attackMob);
    profile.registerRule("legacy.player", attackPlayer);
    profile.registerRule("legacy.nonfocus", useNonFocusSkill);
    return profile;
}
} // namespace game::legacy
