#pragma once

namespace game {

struct DamageResult {
    double applied = 0;
    bool died = false;
};

// Resident-owned health. Healing a dead resident does not revive it.
class Vitality final {
public:
    explicit Vitality(double maximum);
    Vitality(double maximum, double current);

    double current() const noexcept { return current_; }
    double maximum() const noexcept { return maximum_; }
    bool isAlive() const noexcept { return current_ > 0; }

    DamageResult takeDamage(double amount);
    double heal(double amount);
    bool tryRevive(double health);
    void setMaximum(double maximum);

private:
    double maximum_;
    double current_;
};

} // namespace game
