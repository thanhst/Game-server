#pragma once

#include "game/legacy/SkillRules.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace game::legacy {

// Java loads templates by class before skill ID. Shield ID 19 belongs to six
// classes; its level row IDs are also repeated. Never use an ID-only registry.
struct SkillKey {
    std::int32_t classId = 0;
    std::int32_t skillId = 0;
    bool operator<(const SkillKey& other) const noexcept {
        return std::tie(classId, skillId) < std::tie(other.classId, other.skillId);
    }
};

struct ItemTemplate {
    std::int32_t id = 0;
    std::string name;
    std::string description;
    std::int32_t type = 0, gender = 0, level = 0;
    std::int64_t powerRequire = 0;
    std::int32_t resalePrice = 0, icon = 0, part = 0;
    std::int32_t head = -1, body = -1, leg = -1, mountId = -1;
    bool upToUp = false, locked = false;
    nlohmann::json options;
};

struct MobTemplate {
    std::int32_t id = 0;
    std::string name;
    std::int32_t level = 0, type = 0, newType = 0;
    std::int64_t hp = 0;
    std::int32_t rangeMove = 0, speed = 0, dartType = 0;
};

struct NpcTemplate {
    std::int32_t id = 0;
    std::string name;
    std::int32_t head = 0, body = 0, leg = 0;
    nlohmann::json menu;
};

struct MapTemplate {
    std::int32_t id = 0;
    std::string name;
    std::int32_t planet = 0, tileId = 0, backgroundId = 0, type = 0;
    std::int32_t backgroundType = 0, zoneCount = 0;
    nlohmann::json waypoints, mobs, npcs, backgroundItems, effects, eventEffects;
    // SQL has no tile collision grid or pixel dimensions. Do not fabricate them.
};

// An immutable validated definition snapshot. No account/session/actor state is
// stored here. Load a new snapshot completely before publishing shared_ptr<const
// ContentSnapshot>; an existing world can retain its old definitions safely.
class ContentSnapshot final {
public:
    static ContentSnapshot load(const std::filesystem::path& file);
    static ContentSnapshot fromJson(nlohmann::json document);

    const nlohmann::json& document() const noexcept { return document_; }
    const nlohmann::json& table(std::string_view name) const;
    const nlohmann::json& embeddedRow(std::string_view table, std::size_t rowIndex) const;

    const std::map<SkillKey, SkillTemplate>& skills() const noexcept { return skills_; }
    const SkillTemplate& skill(std::int32_t classId, std::int32_t skillId) const;
    const SkillLevel& skillLevel(std::int32_t classId, std::int32_t levelRowId) const;
    const std::map<std::int32_t, ItemTemplate>& items() const noexcept { return items_; }
    const std::map<std::int32_t, MobTemplate>& mobs() const noexcept { return mobs_; }
    const std::map<std::int32_t, NpcTemplate>& npcs() const noexcept { return npcs_; }
    const std::map<std::int32_t, MapTemplate>& maps() const noexcept { return maps_; }
    std::size_t rowCount() const noexcept { return rowCount_; }
    std::size_t skillLevelCount() const noexcept { return skillLevelCount_; }

private:
    ContentSnapshot() = default;
    void validateAndIndex();
    nlohmann::json document_;
    std::map<SkillKey, SkillTemplate> skills_;
    // Resolves level row IDs to a template and its source-order vector index.
    std::map<std::pair<std::int32_t, std::int32_t>, std::pair<SkillKey, std::size_t>> skillLevels_;
    std::map<std::int32_t, ItemTemplate> items_;
    std::map<std::int32_t, MobTemplate> mobs_;
    std::map<std::int32_t, NpcTemplate> npcs_;
    std::map<std::int32_t, MapTemplate> maps_;
    std::size_t rowCount_ = 0, skillLevelCount_ = 0;
};

} // namespace game::legacy
