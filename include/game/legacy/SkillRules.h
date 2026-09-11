#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Selected HUNR2026 rules. These do not change the new data-driven demo rules.
namespace game::legacy {
enum class SkillId : std::int32_t {
    CHIEU_DAM_DRAGON = 0,
    CHIEU_KAMEJOKO = 1,
    CHIEU_DAM_DEMON = 2,
    CHIEU_MASENKO = 3,
    CHIEU_DAM_GALICK = 4,
    CHIEU_ANTOMIC = 5,
    THAI_DUONG_HA_SAN = 6,
    TRI_THUONG = 7,
    TAI_TAO_NANG_LUONG = 8,
    KAIOKEN = 9,
    QUA_CAU_KENH_KHI = 10,
    MAKANKOSAPPO = 11,
    DE_TRUNG = 12,
    BIEN_HINH = 13,
    TU_PHAT_NO = 14,
    DANH = 15,
    CHUONG = 16,
    LIEN_HOAN = 17,
    BIEN_SOCOLA = 18,
    KHIEN_NANG_LUONG = 19,
    DICH_CHUYEN_TUC_THOI = 20,
    HUYT_SAO = 21,
    THOI_MIEN = 22,
    TROI = 23
};

struct NamedSkill { SkillId id; std::string_view name; };
const std::array<NamedSkill, 24>& namedSkills() noexcept;
std::string_view skillName(SkillId id); // Throws for an unknown legacy ID.

struct SkillOption {
    std::int32_t templateId = 0;
    std::int32_t parameter = 0;
    std::string text;
};

// Immutable level data after loading; the Java Skill row ID differs from template ID.
struct SkillLevel {
    std::int32_t id = 0;
    std::int32_t point = 1;
    std::int64_t powerRequire = 0;
    std::int32_t cooldownMs = 0;
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    std::int32_t maxFight = 0;
    std::int32_t manaUse = 0;
    std::int16_t damage = 0;
    std::int16_t price = 0;
    std::vector<SkillOption> options;
    std::string moreInfo;

    void validate() const;
};

struct SkillTemplate {
    SkillId id = SkillId::CHIEU_DAM_DRAGON;
    std::int32_t classId = 0;
    std::string name;
    std::int32_t maxPoint = 7;
    std::int32_t manaUseType = 0; // Java: 1 => percent of max MP; every other value => fixed.
    std::int32_t type = 0;
    std::int32_t icon = 0;
    std::string description;
    std::string damageInfo;
    std::vector<SkillLevel> levels;

    bool isBuffToPlayer() const noexcept { return type == 2; }
    bool isUseAlone() const noexcept { return type == 3; }
    bool isAttackSkill() const noexcept { return type == 1; }
    const SkillLevel& level(std::int32_t point) const;
    void validate() const;
};

// Actor-owned state; never stored on a shared SkillTemplate/SkillLevel.
// Legacy absolute wall-clock elapsed semantics are explicit, not a new-world clock policy.
struct SkillRuntime {
    std::optional<std::int64_t> lastUseMs;
    std::optional<std::int32_t> cooldownOverrideMs;

    std::int32_t effectiveCooldownMs(const SkillLevel& level) const;
    std::int32_t remainingCooldownMs(const SkillLevel& level, std::int64_t nowMs) const;
    bool isCooldown(const SkillLevel& level, std::int64_t nowMs) const;
    void recordUse(std::int64_t nowMs);
};

// floor(value * percentage / 100), with nonnegative operands and checked int64 output.
std::int64_t percentOf(std::int64_t value, std::int32_t percentage);
std::int64_t manaCost(const SkillTemplate& skill, const SkillLevel& level,
                      std::int64_t maxMana, bool boss = false);

// Source skill levels 1..7 only. Values are seconds, unlike cooldown milliseconds.
std::int32_t shieldDurationSeconds(std::int32_t level);
std::int32_t summonDurationSeconds(std::int32_t level);
std::int32_t transformDurationSeconds(std::int32_t level, bool caDicSet = false);
std::int64_t summonHp(std::int64_t maxHp, std::int32_t level);
std::int32_t summonTemplateId(std::int32_t level);

struct ProtectedHit {
    std::int64_t damage;
    bool breaksShield;
};
// Only the isProtected branch at Player.java:3144; caller owns subsequent injury logic.
ProtectedHit protectedHit(std::int64_t incomingDamage, std::int64_t targetMaxHp);
} // namespace game::legacy
