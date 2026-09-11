#include "game/legacy/SkillRules.h"

#include <limits>
#include <set>
#include <stdexcept>

namespace game::legacy {
namespace {
constexpr std::array<NamedSkill, 24> Names{{
    {SkillId::CHIEU_DAM_DRAGON, "CHIEU_DAM_DRAGON"},
    {SkillId::CHIEU_KAMEJOKO, "CHIEU_KAMEJOKO"},
    {SkillId::CHIEU_DAM_DEMON, "CHIEU_DAM_DEMON"},
    {SkillId::CHIEU_MASENKO, "CHIEU_MASENKO"},
    {SkillId::CHIEU_DAM_GALICK, "CHIEU_DAM_GALICK"},
    {SkillId::CHIEU_ANTOMIC, "CHIEU_ANTOMIC"},
    {SkillId::THAI_DUONG_HA_SAN, "THAI_DUONG_HA_SAN"},
    {SkillId::TRI_THUONG, "TRI_THUONG"},
    {SkillId::TAI_TAO_NANG_LUONG, "TAI_TAO_NANG_LUONG"},
    {SkillId::KAIOKEN, "KAIOKEN"},
    {SkillId::QUA_CAU_KENH_KHI, "QUA_CAU_KENH_KHI"},
    {SkillId::MAKANKOSAPPO, "MAKANKOSAPPO"},
    {SkillId::DE_TRUNG, "DE_TRUNG"},
    {SkillId::BIEN_HINH, "BIEN_HINH"},
    {SkillId::TU_PHAT_NO, "TU_PHAT_NO"},
    {SkillId::DANH, "DANH"},
    {SkillId::CHUONG, "CHUONG"},
    {SkillId::LIEN_HOAN, "LIEN_HOAN"},
    {SkillId::BIEN_SOCOLA, "BIEN_SOCOLA"},
    {SkillId::KHIEN_NANG_LUONG, "KHIEN_NANG_LUONG"},
    {SkillId::DICH_CHUYEN_TUC_THOI, "DICH_CHUYEN_TUC_THOI"},
    {SkillId::HUYT_SAO, "HUYT_SAO"},
    {SkillId::THOI_MIEN, "THOI_MIEN"},
    {SkillId::TROI, "TROI"}
}};

void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}

std::int64_t multiply(std::int64_t a, std::int64_t b) {
    if (b != 0 && a > (std::numeric_limits<std::int64_t>::max)() / b)
        throw std::overflow_error("Legacy skill integer multiplication overflow");
    return a * b;
}

void supportedLevel(std::int32_t level) {
    require(level >= 1 && level <= 7, "Selected legacy skill rule requires level 1..7");
}

void nonnegativeTime(std::int64_t time) {
    require(time >= 0, "Legacy skill timestamp must be nonnegative");
}
} // namespace

const std::array<NamedSkill, 24>& namedSkills() noexcept { return Names; }

std::string_view skillName(SkillId id) {
    const auto number = static_cast<std::int32_t>(id);
    if (number < 0 || number >= static_cast<std::int32_t>(Names.size()))
        throw std::invalid_argument("Unknown legacy skill template ID");
    return Names[static_cast<std::size_t>(number)].name;
}

void SkillLevel::validate() const {
    require(id >= 0 && id <= (std::numeric_limits<std::int16_t>::max)(), "Legacy skill row ID must fit a nonnegative Java short");
    require(point >= 0 && point <= 127, "Legacy skill point must be 0..127");
    require(powerRequire >= 0, "Legacy skill power requirement must be nonnegative");
    require(cooldownMs >= 0, "Legacy skill cooldown must be nonnegative");
    require(dx >= 0 && dy >= 0 && maxFight >= 0, "Legacy skill target dimensions/count must be nonnegative");
    require(manaUse >= 0, "Legacy skill mana use must be nonnegative");
    require(options.size() <= 256, "Legacy skill has more than 256 options");
}

const SkillLevel& SkillTemplate::level(std::int32_t requestedPoint) const {
    for (const auto& candidate : levels) if (candidate.point == requestedPoint) return candidate;
    throw std::out_of_range("Legacy skill level is not loaded");
}

void SkillTemplate::validate() const {
    // Database-defined extensions are valid even if they have no named constant.
    const auto wireId = static_cast<std::int32_t>(id);
    require(wireId >= 0 && wireId <= 127, "Legacy skill template ID must fit a nonnegative Java byte");
    require(classId >= 0, "Legacy skill class ID must be nonnegative");
    require(!name.empty(), "Legacy skill template requires a name");
    require(maxPoint >= 1 && maxPoint <= 127, "Legacy skill maxPoint must be 1..127");
    require(levels.size() <= static_cast<std::size_t>(maxPoint), "Too many legacy skill levels");
    std::set<std::int32_t> points, rowIds;
    for (const auto& entry : levels) {
        entry.validate();
        require(entry.point <= maxPoint, "Legacy skill point exceeds template maxPoint");
        require(points.insert(entry.point).second, "Duplicate legacy skill point");
        require(rowIds.insert(entry.id).second, "Duplicate legacy skill row ID");
    }
}

std::int32_t SkillRuntime::effectiveCooldownMs(const SkillLevel& level) const {
    require(level.cooldownMs >= 0, "Legacy skill cooldown must be nonnegative");
    const auto cooldown = cooldownOverrideMs.value_or(level.cooldownMs);
    require(cooldown >= 0, "Legacy skill runtime cooldown must be nonnegative");
    return cooldown;
}

std::int32_t SkillRuntime::remainingCooldownMs(const SkillLevel& level, std::int64_t nowMs) const {
    nonnegativeTime(nowMs);
    const auto cooldown = effectiveCooldownMs(level);
    if (!lastUseMs) return 0;
    nonnegativeTime(*lastUseMs);
    // Same absolute difference as Java Skill.isCooldown(), without abs(INT64_MIN).
    const auto elapsed = nowMs >= *lastUseMs ? nowMs - *lastUseMs : *lastUseMs - nowMs;
    return elapsed >= cooldown ? 0 : cooldown - static_cast<std::int32_t>(elapsed);
}

bool SkillRuntime::isCooldown(const SkillLevel& level, std::int64_t nowMs) const {
    return remainingCooldownMs(level, nowMs) != 0;
}

void SkillRuntime::recordUse(std::int64_t nowMs) {
    nonnegativeTime(nowMs);
    lastUseMs = nowMs;
}

std::int64_t percentOf(std::int64_t value, std::int32_t percentage) {
    require(value >= 0 && percentage >= 0, "Legacy percentage operands must be nonnegative");
    // Splitting before multiplication preserves integer truncation without a wide-integer extension.
    const auto whole = multiply(value / 100, percentage);
    const auto fraction = (value % 100) * static_cast<std::int64_t>(percentage) / 100;
    if (whole > (std::numeric_limits<std::int64_t>::max)() - fraction)
        throw std::overflow_error("Legacy skill percentage overflow");
    return whole + fraction;
}

std::int64_t manaCost(const SkillTemplate& skill, const SkillLevel& level,
                      std::int64_t maxMana, bool boss) {
    require(level.manaUse >= 0 && maxMana >= 0, "Legacy mana values must be nonnegative");
    if (boss) return 0;
    return skill.manaUseType == 1 ? percentOf(maxMana, level.manaUse) : level.manaUse;
}

std::int32_t shieldDurationSeconds(std::int32_t level) {
    supportedLevel(level);
    return 15 + (level - 1) * 5;
}

std::int32_t summonDurationSeconds(std::int32_t level) {
    supportedLevel(level);
    return 55 + 10 * level;
}

std::int32_t transformDurationSeconds(std::int32_t level, bool caDicSet) {
    return summonDurationSeconds(level) * (caDicSet ? 5 : 1);
}

std::int64_t summonHp(std::int64_t maxHp, std::int32_t level) {
    supportedLevel(level);
    require(maxHp > 0, "Legacy summon requires positive caster max HP");
    return multiply(maxHp, level);
}

std::int32_t summonTemplateId(std::int32_t level) {
    supportedLevel(level);
    constexpr std::array<std::int32_t, 7> templates{{8, 11, 32, 25, 43, 49, 50}};
    return templates[static_cast<std::size_t>(level - 1)];
}

ProtectedHit protectedHit(std::int64_t incomingDamage, std::int64_t targetMaxHp) {
    require(incomingDamage >= 0 && targetMaxHp > 0, "Legacy protected hit requires nonnegative damage and positive max HP");
    return {1, incomingDamage >= targetMaxHp};
}
} // namespace game::legacy
