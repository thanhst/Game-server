#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace game {
using EntityId = std::uint64_t;
using Milliseconds = std::uint64_t;
using Attributes = std::map<std::string, double>;
struct Position { double x = 0; double y = 0; };
enum class TargetRule { Self, Enemy, Ally };
enum class Stacking { Refresh, Stack, Replace };

// Definition IDs are strings: adding content does not grow an enum or Player class.
struct EffectDefinition {
    std::string id;
    std::string handler;
    double magnitude = 0;
    std::string scalingAttribute;
    double scalingFactor = 0;
    std::string attribute;
    Milliseconds duration = 0;
    Milliseconds period = 0;
    Stacking stacking = Stacking::Refresh;
    std::uint32_t maxStacks = 1;
    std::set<std::string> tags;
    std::string visual;
};
struct SkillDefinition {
    std::string id;
    std::string name;
    int legacyId = -1;
    double manaCost = 0;
    bool manaPercent = false;
    Milliseconds cooldown = 0;
    double range = 0;
    double radius = 0;
    std::uint32_t maxTargets = 1;
    TargetRule target = TargetRule::Enemy;
    std::vector<std::string> effects;
};
struct CharacterDefinition {
    std::string id;
    std::string name;
    int legacyClass = -1;
    Attributes attributes;
    std::set<std::string> tags;
    std::vector<std::string> skills;
};
struct MapDefinition {
    std::string id;
    double width = 0;
    double height = 0;
};
struct Content {
    std::map<std::string, EffectDefinition> effects;
    std::map<std::string, SkillDefinition> skills;
    std::map<std::string, CharacterDefinition> characters;
    std::map<std::string, MapDefinition> maps;

    // Throws with file/line or reference information; no partial live update.
    static Content load(const std::filesystem::path& file);
    void validate() const;
};
} // namespace game
