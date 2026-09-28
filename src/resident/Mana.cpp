#include "game/resident/Mana.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace game {
namespace {
void requireAmount(double amount) {
    if (!std::isfinite(amount) || amount < 0)
        throw std::invalid_argument("mana amount must be finite and nonnegative");
}
} // namespace

Mana::Mana(double maximum) : Mana(maximum, maximum) {}

Mana::Mana(double maximum, double current)
    : maximum_(maximum), current_(current) {
    requireAmount(maximum);
    requireAmount(current);
    if (current > maximum)
        throw std::invalid_argument("current mana exceeds maximum mana");
}

bool Mana::canSpend(double cost) const {
    requireAmount(cost);
    // Do not accept a positive cost that double precision would round away.
    return cost <= current_ && (cost == 0 || current_ - cost < current_);
}

bool Mana::trySpend(double cost) {
    if (!canSpend(cost)) return false;
    current_ -= cost;
    return true;
}

double Mana::restore(double amount) {
    requireAmount(amount);
    const double before = current_;
    current_ += std::min(amount, maximum_ - current_);
    return current_ - before;
}

void Mana::setMaximum(double maximum) {
    requireAmount(maximum);
    maximum_ = maximum;
    current_ = std::min(current_, maximum_);
}

} // namespace game
