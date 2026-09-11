#include "game/legacy/SkillRules.h"

#include <array>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void checkLegacy(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(std::string("LegacySkillTests: ") + message);
}
template <typename Exception> void rejectsLegacy(const std::function<void()>& action) {
    try { action(); }
    catch (const Exception&) { return; }
    throw std::runtime_error("LegacySkillTests: expected exception was not thrown");
}
} // namespace

void runLegacySkillTests() {
    using namespace game::legacy;
    constexpr std::array<std::string_view, 24> expectedNames{{
        "CHIEU_DAM_DRAGON", "CHIEU_KAMEJOKO", "CHIEU_DAM_DEMON", "CHIEU_MASENKO",
        "CHIEU_DAM_GALICK", "CHIEU_ANTOMIC", "THAI_DUONG_HA_SAN", "TRI_THUONG",
        "TAI_TAO_NANG_LUONG", "KAIOKEN", "QUA_CAU_KENH_KHI", "MAKANKOSAPPO",
        "DE_TRUNG", "BIEN_HINH", "TU_PHAT_NO", "DANH", "CHUONG", "LIEN_HOAN",
        "BIEN_SOCOLA", "KHIEN_NANG_LUONG", "DICH_CHUYEN_TUC_THOI", "HUYT_SAO", "THOI_MIEN", "TROI"
    }};
    for (std::size_t i = 0; i < expectedNames.size(); ++i) {
        checkLegacy(static_cast<std::int32_t>(namedSkills()[i].id) == static_cast<std::int32_t>(i), "legacy numeric ID changed");
        checkLegacy(skillName(static_cast<SkillId>(i)) == expectedNames[i], "legacy skill name changed");
    }
    rejectsLegacy<std::invalid_argument>([] { skillName(static_cast<SkillId>(24)); });
    rejectsLegacy<std::invalid_argument>([] { skillName(static_cast<SkillId>(-1)); });

    SkillLevel level;
    level.id = 101;
    level.cooldownMs = 1000;
    level.manaUse = 15;
    SkillTemplate skill;
    skill.id = SkillId::KHIEN_NANG_LUONG;
    skill.name = "Legacy energy shield";
    skill.type = 3;
    skill.levels.push_back(level);
    skill.validate();
    auto extendedTemplate = skill;
    extendedTemplate.id = static_cast<SkillId>(24);
    extendedTemplate.validate(); // Named constants are not the full content registry.
    checkLegacy(skill.level(1).id == 101, "level row ID differs from template ID");
    checkLegacy(skill.isUseAlone() && !skill.isBuffToPlayer() && !skill.isAttackSkill(), "use-alone type");
    skill.type = 2;
    checkLegacy(skill.isBuffToPlayer() && !skill.isUseAlone(), "buff type");
    skill.type = 1;
    checkLegacy(skill.isAttackSkill() && !skill.isBuffToPlayer(), "attack type");
    rejectsLegacy<std::out_of_range>([&] { skill.level(2); });
    auto duplicate = skill;
    duplicate.levels.push_back(level);
    rejectsLegacy<std::invalid_argument>([&] { duplicate.validate(); });
    auto badLevel = level;
    badLevel.point = -1;
    rejectsLegacy<std::invalid_argument>([&] { badLevel.validate(); });
    badLevel = level;
    badLevel.cooldownMs = -1;
    rejectsLegacy<std::invalid_argument>([&] { badLevel.validate(); });

    checkLegacy(manaCost(skill, level, 999) == 15, "fixed mana cost");
    skill.manaUseType = 1;
    checkLegacy(manaCost(skill, level, 999) == 149, "percent max mana truncation");
    checkLegacy(manaCost(skill, level, 0) == 0, "zero max mana percentage");
    checkLegacy(manaCost(skill, level, 999, true) == 0, "boss mana exemption");
    skill.manaUseType = 2;
    checkLegacy(manaCost(skill, level, 999) == 15, "Java non-one mana type remains fixed");
    constexpr auto largest = (std::numeric_limits<std::int64_t>::max)();
    checkLegacy(percentOf(largest, 100) == largest, "representable percent with overflowing naive intermediate");
    checkLegacy(percentOf(largest, 1) == largest / 100, "one percent of int64 max");
    checkLegacy(percentOf(1, (std::numeric_limits<std::int32_t>::max)()) == 21474836, "large percentage truncation");
    checkLegacy(percentOf(199, 50) == 99, "fraction truncates after multiplication");
    rejectsLegacy<std::overflow_error>([&] { percentOf(largest, 101); });
    rejectsLegacy<std::invalid_argument>([] { percentOf(-1, 10); });
    rejectsLegacy<std::invalid_argument>([] { percentOf(10, -1); });
    rejectsLegacy<std::invalid_argument>([&] { manaCost(skill, level, -1); });

    SkillRuntime alice, bob;
    checkLegacy(!alice.isCooldown(level, 0), "first use is ready even at zero simulation timestamp");
    alice.recordUse(5000);
    checkLegacy(alice.remainingCooldownMs(level, 5000) == 1000, "full cooldown on use");
    checkLegacy(alice.isCooldown(level, 5999) && !alice.isCooldown(level, 6000), "cooldown exact boundary");
    checkLegacy(!bob.isCooldown(level, 5001), "runtime cooldown does not leak between actors");
    alice.cooldownOverrideMs = 250;
    checkLegacy(alice.isCooldown(level, 5249) && !alice.isCooldown(level, 5250), "runtime cooldown override");
    checkLegacy(level.cooldownMs == 1000 && bob.effectiveCooldownMs(level) == 1000, "shared base cooldown stays immutable");
    alice.cooldownOverrideMs.reset();
    checkLegacy(alice.isCooldown(level, 4001) && !alice.isCooldown(level, 4000), "explicit Java absolute time difference");
    alice.recordUse(largest);
    checkLegacy(!alice.isCooldown(level, 0), "large absolute elapsed does not overflow");
    alice.cooldownOverrideMs = 0;
    checkLegacy(!alice.isCooldown(level, largest), "zero cooldown");
    alice.cooldownOverrideMs = -1;
    rejectsLegacy<std::invalid_argument>([&] { alice.isCooldown(level, largest); });
    rejectsLegacy<std::invalid_argument>([&] { bob.recordUse(-1); });

    constexpr std::array<std::int32_t, 7> summonIds{{8, 11, 32, 25, 43, 49, 50}};
    for (std::int32_t point = 1; point <= 7; ++point) {
        checkLegacy(shieldDurationSeconds(point) == 15 + 5 * (point - 1), "source shield duration");
        checkLegacy(summonDurationSeconds(point) == 55 + 10 * point, "source summon duration");
        checkLegacy(transformDurationSeconds(point) == summonDurationSeconds(point), "base transform duration");
        checkLegacy(transformDurationSeconds(point, true) == summonDurationSeconds(point) * 5, "CaDic set duration multiplier");
        checkLegacy(summonHp(1001, point) == 1001 * point, "summon HP multiplier");
        checkLegacy(summonTemplateId(point) == summonIds[static_cast<std::size_t>(point - 1)], "summon template mapping");
    }
    checkLegacy(shieldDurationSeconds(1) == 15 && shieldDurationSeconds(7) == 45, "shield endpoint durations");
    checkLegacy(summonDurationSeconds(1) == 65 && summonDurationSeconds(7) == 125, "summon endpoint durations");
    checkLegacy(summonHp(largest, 1) == largest, "summon HP representable boundary");
    rejectsLegacy<std::overflow_error>([&] { summonHp(largest, 2); });
    rejectsLegacy<std::invalid_argument>([] { summonHp(0, 1); });
    rejectsLegacy<std::invalid_argument>([] { shieldDurationSeconds(0); });
    rejectsLegacy<std::invalid_argument>([] { summonTemplateId(8); });

    const auto protectedSmall = protectedHit(99, 100);
    checkLegacy(protectedSmall.damage == 1 && !protectedSmall.breaksShield, "legacy shield reduces a protected hit to one");
    const auto protectedLarge = protectedHit(100, 100);
    checkLegacy(protectedLarge.damage == 1 && protectedLarge.breaksShield, "legacy shield break threshold remains max HP");
    checkLegacy(protectedHit(0, 100).damage == 1, "exact protected branch also maps zero incoming damage to one");
    rejectsLegacy<std::invalid_argument>([] { protectedHit(-1, 100); });
}
