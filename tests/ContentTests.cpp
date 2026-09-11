#include "game/Content.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {
void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("ContentTests: " + message);
}

void rejects(const std::function<void()>& operation, const std::string& expected) {
    try { operation(); }
    catch (const std::runtime_error& error) {
        check(std::string(error.what()).find(expected) != std::string::npos,
              "expected error containing '" + expected + "', got: " + error.what());
        return;
    }
    throw std::runtime_error("ContentTests: invalid content accepted; expected " + expected);
}

class TempContent {
public:
    TempContent() {
        static std::atomic<unsigned> sequence{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            const auto candidate = std::filesystem::temp_directory_path() /
                ("gameserver-content-tests-" + std::to_string(stamp) + "-" + std::to_string(sequence++));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) { directory_ = candidate; return; }
            if (error) throw std::runtime_error("ContentTests: cannot create temp directory: " + error.message());
        }
        throw std::runtime_error("ContentTests: cannot allocate unique temp directory");
    }
    ~TempContent() {
        std::error_code ignored;
        if (!directory_.empty()) std::filesystem::remove_all(directory_, ignored);
    }
    TempContent(const TempContent&) = delete;
    TempContent& operator=(const TempContent&) = delete;
    std::filesystem::path write(const std::string& text) const {
        const auto file = directory_ / "case.game";
        std::ofstream output(file, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.close();
        check(!output.fail(), "cannot write fixture");
        return file;
    }
    game::Content load(const std::string& text) const { return game::Content::load(write(text)); }
private:
    std::filesystem::path directory_;
};

const std::string Base = R"GAME(
# Forward references are intentional.
[character fighter]
name=Test fighter
stat.hp=100
stat.mana=50
stat.attack=12
stat.defense=3
stat.speed=6
tags=player,test
skills=strike
[skill strike]
name=Test strike
mana_cost=10
mana_percent=true
cooldown_ms=500
range=3
target=enemy
effects=hit
[effect hit]
handler=damage
magnitude=4
scaling_attribute=attack
scaling_factor=1
[map arena]
width=500
height=500
)GAME";
} // namespace

void runContentTests() {
    TempContent fixture;
    auto valid = fixture.load("\xEF\xBB\xBF" + Base);
    check(valid.characters.at("fighter").skills.at(0) == "strike", "forward character reference");
    check(valid.skills.at("strike").manaPercent && valid.skills.at("strike").manaCost == 10, "percent mana parse");
    check(valid.effects.at("hit").scalingAttribute == "attack", "scaling attribute parse");
    check(valid.effects.at("hit").duration == 0 && valid.effects.at("hit").maxStacks == 1, "effect defaults");
    check(valid.maps.at("arena").width == 500, "map parse");

    const auto rejectText = [&](const std::string& text, const std::string& message) {
        rejects([&] { fixture.load(text); }, message);
    };
    rejectText("name=orphan\n", ":1:");
    rejectText("[effect]\n", "section requires kind and id");
    rejectText("[effect bad id]\n", "invalid identifier");
    rejectText("[unknown x]\n", "unknown section kind");
    rejectText("[map x]\nwidth=1\nheight=1\nextra=1\n", "unknown map key");
    rejectText("[map x]\nwidth=1\nwidth=2\nheight=1\n", "duplicate key");
    rejectText(Base + "[effect hit]\nhandler=heal\n", "duplicate effect section");
    rejectText("[effect x]\nhandler=damage\ntags=a,a\n", "duplicate list item");
    rejectText("[effect x]\nhandler=damage\ntags=a,\n", "invalid identifier");
    rejectText("[effect x]\nhandler=damage\nmagnitude=\n", "empty key or value");
    rejectText("[skill x]\nname=X\neffects=absent\n", "unknown effect 'absent'");
    rejectText(Base + "[character other]\nname=Other\nstat.hp=1\n", "missing stat.mana");
    rejectText(Base + "[skill x]\nname=X\nmana_percent=yes\neffects=hit\n", "boolean must be");
    rejectText("[effect x]\nhandler=damage\nstacking=typo\n", "stacking must be");
    rejectText("[effect x]\nhandler=damage\nduration_ms=-1\n", "invalid or overflowing integer");
    rejectText("[effect x]\nhandler=damage\nduration_ms=18446744073709551616\n", "overflowing integer");
    rejectText("[effect x]\nhandler=damage\nduration_ms=1.5\n", "invalid or overflowing integer");
    rejectText(std::string(16385, '#') + "\n", "line exceeds");
    for (const auto* value : {"nan", "inf", "-inf", "1e309", "1e-999", "0x10", "1,5", "1 trailing", "1e", "."})
        rejectText("[effect x]\nhandler=damage\nmagnitude=" + std::string(value) + "\n", "number");
    const auto decimals = fixture.load("[effect x]\nhandler=damage\nmagnitude=+1.25e2\n");
    check(decimals.effects.at("x").magnitude == 125, "decimal and exponent parse");

    // A failed load cannot partially replace an already accepted pack.
    rejects([&] { valid = fixture.load(Base + "[map broken]\nwidth=0\nheight=1\n"); }, "must be positive");
    check(valid.maps.size() == 1 && valid.maps.count("arena") == 1, "transactional load");

    const auto rejectChange = [&](const std::function<void(game::Content&)>& change, const std::string& message) {
        auto candidate = valid;
        change(candidate);
        rejects([&] { candidate.validate(); }, message);
    };
    rejectChange([](auto& c) { c.effects.at("hit").magnitude = std::numeric_limits<double>::infinity(); }, "number out of range");
    rejectChange([](auto& c) { c.characters.at("fighter").attributes["hp"] = 0; }, "hp and speed must be positive");
    rejectChange([](auto& c) { c.characters.at("fighter").attributes["mana"] = -1; }, "must be nonnegative");
    rejectChange([](auto& c) { c.characters.at("fighter").skills.push_back("unknown"); }, "unknown skill");
    rejectChange([](auto& c) { c.characters.at("fighter").skills.push_back("strike"); }, "duplicate list item");
    rejectChange([](auto& c) { c.skills.at("strike").effects.clear(); }, "must not be empty");
    rejectChange([](auto& c) { c.skills.at("strike").maxTargets = 1025; }, "max_targets must be");
    rejectChange([](auto& c) { c.skills.at("strike").manaCost = 101; }, "number out of range");
    rejectChange([](auto& c) { c.skills.at("strike").target = static_cast<game::TargetRule>(99); }, "invalid target enum");
    rejectChange([](auto& c) { c.effects.at("hit").id = "different"; }, "map key differs");
    rejectChange([](auto& c) { c.effects.at("hit").scalingAttribute.clear(); }, "scaling_factor requires");
    rejectChange([](auto& c) { c.effects.at("hit").duration = 86400001; }, "exceeds 24 hours");
    rejectChange([](auto& c) { c.effects.at("hit").maxStacks = 101; }, "max_stacks must be");
    rejectChange([](auto& c) { c.effects.at("hit").stacking = static_cast<game::Stacking>(99); }, "invalid stacking enum");
    rejectChange([](auto& c) { c.effects.at("hit").duration = 100; }, "must be instant");
    rejectChange([](auto& c) { c.effects.at("hit").handler = "modifier"; c.effects.at("hit").duration = 100; }, "modifier attribute");
    rejectChange([](auto& c) { c.effects.at("hit").handler = "control"; c.effects.at("hit").duration = 100; }, "control requires tags");
    rejectChange([](auto& c) { c.effects.at("hit").handler = "periodic_damage"; c.effects.at("hit").duration = 100; c.effects.at("hit").period = 1; }, "period >= 50 ms");
    rejectChange([](auto& c) { c.effects.at("hit").period = 100; }, "period requires duration");

    // New stat, effect, skill, and archetype work using only additional data.
    const auto extended = fixture.load(Base + R"GAME(
[effect frost]
handler=periodic_damage
magnitude=3
scaling_attribute=spirit
scaling_factor=0.25
duration_ms=3000
period_ms=1000
stacking=stack
max_stacks=2
tags=frost
[skill frost_bolt]
name=Frost bolt
mana_cost=8
cooldown_ms=1500
range=20
effects=frost
[character frost_guardian]
name=Frost guardian
stat.hp=130
stat.mana=90
stat.attack=14
stat.defense=6
stat.speed=7
stat.spirit=20
tags=player,frost
skills=strike,frost_bolt
)GAME");
    check(extended.characters.at("frost_guardian").attributes.at("spirit") == 20, "data-only attribute extension");
    check(extended.skills.at("frost_bolt").effects.at(0) == "frost", "data-only skill extension");
    check(extended.effects.at("frost").maxStacks == 2, "data-only stacking parse");

    auto customHandler = valid;
    customHandler.effects.at("hit").handler = "plugin.frost_explosion";
    customHandler.validate(); // Registry, not parser, decides whether a handler is installed.
}
