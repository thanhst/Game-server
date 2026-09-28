#pragma once

namespace game {

// Resident-owned mana. A resident with no mana has a maximum of zero.
class Mana final {
public:
    explicit Mana(double maximum);
    Mana(double maximum, double current);

    double current() const noexcept { return current_; }
    double maximum() const noexcept { return maximum_; }

    bool canSpend(double cost) const;
    bool trySpend(double cost);
    double restore(double amount);
    void setMaximum(double maximum);

private:
    double maximum_;
    double current_;
};

} // namespace game
