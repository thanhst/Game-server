#include "game/Content.h"

#include <charconv>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace game {
namespace {
constexpr std::size_t MaxFileBytes = 64 * 1024 * 1024;
constexpr std::size_t MaxLineBytes = 16384;
constexpr std::size_t MaxDefinitions = 10000;
constexpr std::size_t MaxListItems = 256;
constexpr Milliseconds MaxTime = 24 * 60 * 60 * 1000;
constexpr double MaxValue = 1e9;

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

std::string trim(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t\r");
    if (begin == std::string_view::npos) return {};
    const auto end = value.find_last_not_of(" \t\r");
    return std::string(value.substr(begin, end - begin + 1));
}

bool identifier(const std::string& value) {
    if (value.empty() || value.size() > 128) return false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto c = value[i];
        const bool base = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '_';
        if (!base && (i == 0 || (c != '-' && c != '.'))) return false;
    }
    return true;
}

void require(bool condition, const std::string& message) {
    if (!condition) fail(message);
}

void checkId(const std::string& value, const std::string& context) {
    require(identifier(value), context + ": invalid identifier '" + value + "'");
}

void checkText(const std::string& value, const std::string& context, bool optional = false) {
    require((optional || !value.empty()) && value.size() <= 512, context + ": text must contain 1..512 bytes");
    for (const auto c : value)
        require(static_cast<unsigned char>(c) >= 32 && c != 127, context + ": control characters are forbidden");
}

void checkValue(double value, const std::string& context, double low = -MaxValue, double high = MaxValue) {
    require(std::isfinite(value) && value >= low && value <= high, context + ": number out of range");
}

double number(const std::string& value) {
    // Restrict lexical syntax as well as conversion: no hex, NaN, infinity, or locale separators.
    std::size_t i = 0;
    if (i < value.size() && (value[i] == '+' || value[i] == '-')) ++i;
    bool digits = false;
    while (i < value.size() && value[i] >= '0' && value[i] <= '9') { ++i; digits = true; }
    if (i < value.size() && value[i] == '.') {
        ++i;
        while (i < value.size() && value[i] >= '0' && value[i] <= '9') { ++i; digits = true; }
    }
    require(digits, "invalid decimal number '" + value + "'");
    if (i < value.size() && (value[i] == 'e' || value[i] == 'E')) {
        ++i;
        if (i < value.size() && (value[i] == '+' || value[i] == '-')) ++i;
        const auto start = i;
        while (i < value.size() && value[i] >= '0' && value[i] <= '9') ++i;
        require(i > start, "invalid exponent in decimal number '" + value + "'");
    }
    require(i == value.size(), "invalid decimal number '" + value + "'");
    double result = 0;
    const char* first = value.data() + (value.front() == '+' ? 1 : 0);
    const auto converted = std::from_chars(first, value.data() + value.size(), result, std::chars_format::general);
    require(converted.ec == std::errc{} && converted.ptr == value.data() + value.size() && std::isfinite(result),
            "nonfinite or unrepresentable number '" + value + "'");
    return result;
}

template <typename Integer> Integer integer(const std::string& value) {
    require(!value.empty(), "empty integer");
    Integer result{};
    const auto converted = std::from_chars(value.data(), value.data() + value.size(), result);
    require(converted.ec == std::errc{} && converted.ptr == value.data() + value.size(),
            "invalid or overflowing integer '" + value + "'");
    return result;
}

bool boolean(const std::string& value) {
    require(value == "true" || value == "false", "boolean must be true or false");
    return value == "true";
}

std::vector<std::string> list(const std::string& value) {
    std::vector<std::string> result;
    std::set<std::string> unique;
    std::size_t start = 0;
    do {
        const auto end = value.find(',', start);
        auto item = trim(std::string_view(value).substr(start, end == std::string::npos ? end : end - start));
        checkId(item, "list item");
        require(unique.insert(item).second, "duplicate list item '" + item + "'");
        require(result.size() < MaxListItems, "list exceeds 256 items");
        result.push_back(std::move(item));
        if (end == std::string::npos) break;
        start = end + 1;
    } while (true);
    return result;
}

std::set<std::string> tags(const std::string& value) {
    const auto items = list(value);
    return {items.begin(), items.end()};
}

template <typename Container> void checkList(const Container& values, const std::string& context) {
    require(values.size() <= MaxListItems, context + ": list exceeds 256 items");
    std::set<std::string> unique;
    for (const auto& value : values) {
        checkId(value, context);
        require(unique.insert(value).second, context + ": duplicate list item '" + value + "'");
    }
}

void setEffect(EffectDefinition& d, const std::string& key, const std::string& value) {
    if (key == "handler") d.handler = value;
    else if (key == "magnitude") d.magnitude = number(value);
    else if (key == "scaling_attribute") d.scalingAttribute = value;
    else if (key == "scaling_factor") d.scalingFactor = number(value);
    else if (key == "attribute") d.attribute = value;
    else if (key == "duration_ms") d.duration = integer<Milliseconds>(value);
    else if (key == "period_ms") d.period = integer<Milliseconds>(value);
    else if (key == "max_stacks") d.maxStacks = integer<std::uint32_t>(value);
    else if (key == "tags") d.tags = tags(value);
    else if (key == "visual") d.visual = value;
    else if (key == "stacking") {
        if (value == "refresh") d.stacking = Stacking::Refresh;
        else if (value == "stack") d.stacking = Stacking::Stack;
        else if (value == "replace") d.stacking = Stacking::Replace;
        else fail("stacking must be refresh, stack, or replace");
    } else fail("unknown effect key '" + key + "'");
}

void setSkill(SkillDefinition& d, const std::string& key, const std::string& value) {
    if (key == "name") d.name = value;
    else if (key == "legacy_id") d.legacyId = integer<int>(value);
    else if (key == "mana_cost") d.manaCost = number(value);
    else if (key == "mana_percent") d.manaPercent = boolean(value);
    else if (key == "cooldown_ms") d.cooldown = integer<Milliseconds>(value);
    else if (key == "range") d.range = number(value);
    else if (key == "radius") d.radius = number(value);
    else if (key == "max_targets") d.maxTargets = integer<std::uint32_t>(value);
    else if (key == "effects") d.effects = list(value);
    else if (key == "target") {
        if (value == "self") d.target = TargetRule::Self;
        else if (value == "enemy") d.target = TargetRule::Enemy;
        else if (value == "ally") d.target = TargetRule::Ally;
        else fail("target must be self, enemy, or ally");
    } else fail("unknown skill key '" + key + "'");
}

void setCharacter(CharacterDefinition& d, const std::string& key, const std::string& value) {
    if (key == "name") d.name = value;
    else if (key == "legacy_class") d.legacyClass = integer<int>(value);
    else if (key == "skills") d.skills = list(value);
    else if (key == "tags") d.tags = tags(value);
    else if (key.compare(0, 5, "stat.") == 0) {
        const auto id = key.substr(5);
        checkId(id, "attribute");
        d.attributes.emplace(id, number(value));
    } else fail("unknown character key '" + key + "'");
}

template <typename Definitions> void checkDefinitions(const Definitions& definitions, const char* kind) {
    require(definitions.size() <= MaxDefinitions, std::string(kind) + ": too many definitions");
    for (const auto& pair : definitions) {
        checkId(pair.first, kind);
        require(pair.first == pair.second.id, std::string(kind) + " '" + pair.first + "': map key differs from definition id");
    }
}
} // namespace

Content Content::load(const std::filesystem::path& file) {
    std::error_code error;
    const auto bytes = std::filesystem::file_size(file, error);
    require(!error && bytes <= MaxFileBytes, file.u8string() + ": missing/unreadable file or file exceeds 64 MiB");
    std::ifstream input(file, std::ios::binary);
    require(input.is_open(), file.u8string() + ": cannot open file");
    Content candidate;
    std::string kind, id, line;
    std::set<std::string> keys;
    std::size_t lineNumber = 0, totalBytes = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        totalBytes += line.size() + 1;
        try {
            require(totalBytes <= MaxFileBytes + 1, "file exceeds 64 MiB");
            require(line.size() <= MaxLineBytes, "line exceeds 16384 bytes");
            if (lineNumber == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
            line = trim(line);
            if (line.empty() || line.front() == '#' || line.front() == ';') continue;
            if (line.front() == '[') {
                require(line.back() == ']', "malformed section header");
                const auto section = trim(std::string_view(line).substr(1, line.size() - 2));
                const auto split = section.find_first_of(" \t");
                require(split != std::string::npos, "section requires kind and id");
                kind = section.substr(0, split);
                id = trim(std::string_view(section).substr(split + 1));
                checkId(id, "section");
                bool inserted = false;
                if (kind == "effect") {
                    require(candidate.effects.size() < MaxDefinitions, "too many effects");
                    EffectDefinition d; d.id = id;
                    inserted = candidate.effects.emplace(id, std::move(d)).second;
                } else if (kind == "skill") {
                    require(candidate.skills.size() < MaxDefinitions, "too many skills");
                    SkillDefinition d; d.id = id;
                    inserted = candidate.skills.emplace(id, std::move(d)).second;
                } else if (kind == "character") {
                    require(candidate.characters.size() < MaxDefinitions, "too many characters");
                    CharacterDefinition d; d.id = id;
                    inserted = candidate.characters.emplace(id, std::move(d)).second;
                } else if (kind == "map") {
                    require(candidate.maps.size() < MaxDefinitions, "too many maps");
                    MapDefinition d; d.id = id;
                    inserted = candidate.maps.emplace(id, std::move(d)).second;
                } else fail("unknown section kind '" + kind + "'");
                require(inserted, "duplicate " + kind + " section '" + id + "'");
                keys.clear();
                continue;
            }
            require(!kind.empty(), "key appears before a section");
            const auto split = line.find('=');
            require(split != std::string::npos, "expected key=value");
            const auto key = trim(std::string_view(line).substr(0, split));
            const auto value = trim(std::string_view(line).substr(split + 1));
            require(!key.empty() && !value.empty(), "empty key or value");
            require(keys.insert(key).second, "duplicate key '" + key + "'");
            if (kind == "effect") setEffect(candidate.effects.at(id), key, value);
            else if (kind == "skill") setSkill(candidate.skills.at(id), key, value);
            else if (kind == "character") setCharacter(candidate.characters.at(id), key, value);
            else {
                auto& d = candidate.maps.at(id);
                if (key == "width") d.width = number(value);
                else if (key == "height") d.height = number(value);
                else fail("unknown map key '" + key + "'");
            }
        } catch (const std::exception& e) {
            fail(file.u8string() + ":" + std::to_string(lineNumber) + ": " + e.what());
        }
    }
    require(!input.bad(), file.u8string() + ": read failed");
    try { candidate.validate(); }
    catch (const std::exception& e) { fail(file.u8string() + ": " + e.what()); }
    return candidate;
}

void Content::validate() const {
    checkDefinitions(effects, "effect");
    checkDefinitions(skills, "skill");
    checkDefinitions(characters, "character");
    checkDefinitions(maps, "map");
    for (const auto& pair : effects) {
        const auto& d = pair.second;
        const auto context = "effect '" + d.id + "'";
        checkId(d.handler, context + " handler");
        checkValue(d.magnitude, context + " magnitude");
        checkValue(d.scalingFactor, context + " scaling_factor", -1e6, 1e6);
        if (!d.scalingAttribute.empty()) checkId(d.scalingAttribute, context + " scaling_attribute");
        else require(d.scalingFactor == 0, context + ": scaling_factor requires scaling_attribute");
        if (!d.attribute.empty()) checkId(d.attribute, context + " attribute");
        require(d.duration <= MaxTime && d.period <= MaxTime, context + ": duration/period exceeds 24 hours");
        require(d.period == 0 || (d.duration > 0 && d.period <= d.duration), context + ": period requires duration >= period");
        require(d.maxStacks >= 1 && d.maxStacks <= 100, context + ": max_stacks must be 1..100");
        require(d.stacking == Stacking::Refresh || d.stacking == Stacking::Stack || d.stacking == Stacking::Replace,
                context + ": invalid stacking enum");
        require(d.stacking == Stacking::Stack || d.maxStacks == 1, context + ": only stack policy can use max_stacks > 1");
        require(d.duration > 0 || (d.stacking != Stacking::Stack && d.maxStacks == 1), context + ": instant effects cannot stack");
        if (d.handler == "damage" || d.handler == "heal")
            require(d.duration == 0 && d.period == 0, context + ": damage/heal must be instant");
        if (d.handler == "periodic_damage")
            require(d.duration > 0 && d.period >= 50, context + ": periodic_damage requires duration and period >= 50 ms");
        if (d.handler == "modifier" || d.handler == "control" || d.handler == "shield")
            require(d.duration > 0 && d.period == 0, context + ": modifier/control/shield require duration and zero period");
        if (d.handler == "modifier") checkId(d.attribute, context + " modifier attribute");
        if (d.handler == "control") require(!d.tags.empty(), context + ": control requires tags");
        if (d.handler == "damage" || d.handler == "heal" || d.handler == "periodic_damage" || d.handler == "shield")
            require(d.magnitude >= 0 && d.scalingFactor >= 0, context + ": magnitude/scaling_factor must be nonnegative for this handler");
        checkList(d.tags, context + " tags");
        checkText(d.visual, context + " visual", true);
    }
    for (const auto& pair : skills) {
        const auto& d = pair.second;
        const auto context = "skill '" + d.id + "'";
        checkText(d.name, context + " name");
        require(d.legacyId >= -1, context + ": legacy_id must be -1 or nonnegative");
        checkValue(d.manaCost, context + " mana_cost", 0, d.manaPercent ? 100 : MaxValue);
        require(d.cooldown <= MaxTime, context + ": cooldown exceeds 24 hours");
        checkValue(d.range, context + " range", 0, 1e6);
        checkValue(d.radius, context + " radius", 0, 1e6);
        require(d.maxTargets >= 1 && d.maxTargets <= 1024, context + ": max_targets must be 1..1024");
        require(d.target == TargetRule::Self || d.target == TargetRule::Enemy || d.target == TargetRule::Ally,
                context + ": invalid target enum");
        require(!d.effects.empty(), context + ": effects list must not be empty");
        checkList(d.effects, context + " effects");
        for (const auto& effect : d.effects)
            require(effects.count(effect) != 0, context + ": unknown effect '" + effect + "'");
    }
    for (const auto& pair : characters) {
        const auto& d = pair.second;
        const auto context = "character '" + d.id + "'";
        checkText(d.name, context + " name");
        require(d.legacyClass >= -1, context + ": legacy_class must be -1 or nonnegative");
        require(d.attributes.size() <= MaxListItems, context + ": at most 256 attributes allowed");
        for (const auto& stat : d.attributes) {
            checkId(stat.first, context + " attribute");
            checkValue(stat.second, context + " stat." + stat.first);
        }
        for (const auto* stat : {"hp", "mana", "attack", "defense", "speed"}) {
            const auto it = d.attributes.find(stat);
            require(it != d.attributes.end(), context + ": missing stat." + stat);
            require(it->second >= 0, context + ": stat." + stat + " must be nonnegative");
        }
        require(d.attributes.at("hp") > 0 && d.attributes.at("speed") > 0, context + ": hp and speed must be positive");
        checkList(d.tags, context + " tags");
        checkList(d.skills, context + " skills");
        for (const auto& skill : d.skills)
            require(skills.count(skill) != 0, context + ": unknown skill '" + skill + "'");
    }
    for (const auto& pair : maps) {
        const auto& d = pair.second;
        const auto context = "map '" + d.id + "'";
        checkValue(d.width, context + " width", 0, 1e6);
        checkValue(d.height, context + " height", 0, 1e6);
        require(d.width > 0 && d.height > 0, context + ": width and height must be positive");
    }
}
} // namespace game
