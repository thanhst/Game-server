#pragma once

#include "game/object/WorldObject.h"
#include "game/resident/Mana.h"
#include "game/resident/Vitality.h"

#include <string>

namespace game {

struct ResidentInit {
    ObjectId id;
    std::string definition;
    std::string mapInstanceId;
    Position position;
    Vitality vitality;
    Mana mana;
    double bodyRadius = 0.5;
};

class Resident : public WorldObject {
public:
    ~Resident() noexcept override = default;

    const std::string& definition() const noexcept { return definition_; }
    bool isAlive() const noexcept { return vitality_.isAlive(); }
    const Vitality& vitality() const noexcept { return vitality_; }
    const Mana& mana() const noexcept { return mana_; }
    double bodyRadius() const noexcept { return bodyRadius_; }

    DamageResult receiveDamage(double amount) { return vitality_.takeDamage(amount); }
    double receiveHealing(double amount) { return vitality_.heal(amount); }
    bool revive(double health) { return vitality_.tryRevive(health); }

    bool canSpendMana(double cost) const { return mana_.canSpend(cost); }
    bool spendMana(double cost) { return mana_.trySpend(cost); }
    double restoreMana(double amount) { return mana_.restore(amount); }

    void setMaximumHealth(double maximum) { vitality_.setMaximum(maximum); }
    void setMaximumMana(double maximum) { mana_.setMaximum(maximum); }

protected:
    Resident(ObjectKind kind, ResidentInit init);

private:
    std::string definition_;
    Vitality vitality_;
    Mana mana_;
    double bodyRadius_;
};

} // namespace game
