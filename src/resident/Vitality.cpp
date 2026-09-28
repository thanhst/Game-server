#include "game/resident/Vitality.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace game {
namespace {
void requireMaximum(double maximum) {
    if (!std::isfinite(maximum) || maximum <= 0)
        throw std::invalid_argument("maximum health must be finite and positive");
}

void requireAmount(double amount) {
    if (!std::isfinite(amount) || amount < 0)
        throw std::invalid_argument("health amount must be finite and nonnegative");
}
} // namespace

Vitality::Vitality(double maximum) : Vitality(maximum, maximum) {}

Vitality::Vitality(double maximum, double current)
    : maximum_(maximum), current_(current) {
    requireMaximum(maximum);
    requireAmount(current);
    if (current > maximum)
        throw std::invalid_argument("current health exceeds maximum health");
}

DamageResult Vitality::takeDamage(double amount) {
    requireAmount(amount);
    if (!isAlive()) return {};

    const double before = current_;
    current_ -= std::min(amount, current_);
    return {before - current_, before > 0 && current_ == 0};
}

double Vitality::heal(double amount) {
    requireAmount(amount);
    if (!isAlive()) return 0;

    const double before = current_;
    current_ += std::min(amount, maximum_ - current_);
    return current_ - before;
}

bool Vitality::tryRevive(double health) {
    if (!std::isfinite(health) || health <= 0)
        throw std::invalid_argument("revive health must be finite and positive");
    if (isAlive()) return false;

    current_ = std::min(health, maximum_);
    return true;
}

void Vitality::setMaximum(double maximum) {
    requireMaximum(maximum);
    maximum_ = maximum;
    current_ = std::min(current_, maximum_);
}

} // namespace game
