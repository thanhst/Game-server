#include "game/resident/Mana.h"
#include "game/resident/Vitality.h"

#include <limits>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Operation>
void rejectsInvalid(Operation&& operation, const char* message) {
    try {
        operation();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(message);
}

void vitalityLifecycle() {
    game::Vitality health(100);
    check(health.current() == 100 && health.maximum() == 100, "health starts full");

    const auto first = health.takeDamage(30);
    check(first.applied == 30 && !first.died && health.current() == 70, "nonlethal damage");
    check(health.heal(50) == 30 && health.current() == 100, "healing clamps to maximum");

    const auto lethal = health.takeDamage(150);
    check(lethal.applied == 100 && lethal.died && !health.isAlive(), "lethal damage");
    const auto afterDeath = health.takeDamage(10);
    check(afterDeath.applied == 0 && !afterDeath.died,
        "death transition happens only once");
    check(health.heal(20) == 0 && health.current() == 0, "ordinary healing cannot revive");
    check(health.tryRevive(40) && health.current() == 40, "explicit revive");
    check(!health.tryRevive(10) && health.current() == 40, "living resident cannot revive again");

    health.setMaximum(25);
    check(health.current() == 25 && health.maximum() == 25, "lower maximum clamps health");
    health.setMaximum(50);
    check(health.current() == 25, "raising maximum does not heal");
}

void manaTransactions() {
    game::Mana mana(100, 40);
    check(!mana.trySpend(50) && mana.current() == 40, "failed spend must not charge mana");
    check(mana.canSpend(40) && mana.trySpend(40) && mana.current() == 0, "exact spend");
    check(mana.restore(200) == 100 && mana.current() == 100, "restore clamps to maximum");

    mana.setMaximum(30);
    check(mana.current() == 30 && mana.maximum() == 30, "lower maximum clamps mana");
    mana.setMaximum(60);
    check(mana.current() == 30, "raising maximum does not restore mana");

    game::Mana noMana(0);
    check(noMana.current() == 0 && noMana.trySpend(0), "zero mana is valid");
    check(!noMana.trySpend(1) && noMana.restore(1) == 0, "zero mana cannot be spent or restored");
}

void invalidInputsDoNotMutate() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    rejectsInvalid([&] { game::Vitality invalid(0); }, "zero maximum health accepted");
    rejectsInvalid([&] { game::Vitality invalid(100, 101); }, "health above maximum accepted");
    rejectsInvalid([&] { game::Mana invalid(-1); }, "negative maximum mana accepted");
    rejectsInvalid([&] { game::Mana invalid(10, infinity); }, "infinite current mana accepted");

    game::Vitality health(100);
    rejectsInvalid([&] { health.takeDamage(nan); }, "NaN damage accepted");
    rejectsInvalid([&] { health.heal(-1); }, "negative healing accepted");
    rejectsInvalid([&] { health.tryRevive(0); }, "zero revive health accepted");
    rejectsInvalid([&] { health.setMaximum(infinity); }, "infinite maximum health accepted");
    check(health.current() == 100 && health.maximum() == 100, "invalid health input mutated state");

    game::Mana mana(50);
    rejectsInvalid([&] { mana.trySpend(-1); }, "negative mana cost accepted");
    rejectsInvalid([&] { mana.restore(nan); }, "NaN mana restoration accepted");
    rejectsInvalid([&] { mana.setMaximum(infinity); }, "infinite maximum mana accepted");
    check(mana.current() == 50 && mana.maximum() == 50, "invalid mana input mutated state");

    game::Mana largeMana(1.0e100);
    check(!largeMana.trySpend(1) && largeMana.current() == largeMana.maximum(),
        "unrepresentable mana cost was accepted without being charged");
}
} // namespace

void runResourceTests() {
    vitalityLifecycle();
    manaTransactions();
    invalidInputsDoNotMutate();
}
