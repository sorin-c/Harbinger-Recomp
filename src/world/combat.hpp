// The original's combat arithmetic: its random numbers, an agent's hit
// points, and whether an attack hits and for how much.
// Notes and evidence: docs/combat.md
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "formats/agt.hpp"
#include "formats/wpn.hpp"

namespace harbinger::world {

// FUN_004f9050: two multiply-with-carry halves (18000 and 0x78b7) joined
// into 32 bits; FUN_004f9030 seeds them with s | 1 and s | 2.
class Rng {
public:
    explicit Rng(uint32_t seed = 0) { this->seed(seed); }
    void seed(uint32_t s) { a_ = s | 1, b_ = s | 2; }
    uint32_t next() {
        a_ = (a_ & 0xffff) * 18000 + (a_ >> 16);
        b_ = (b_ & 0xffff) * 0x78b7 + (b_ >> 16);
        return (b_ & 0xffff) + (a_ << 16);
    }

private:
    uint32_t a_ = 1, b_ = 2;
};

// A damage level's range of hit points (the table at 0x54ffb0, rows of
// min, max and a third word; levels 1-20). Types 5-8 use 0x550028 instead.
struct DamageRange {
    uint16_t min = 0, max = 0;
};
DamageRange damageRange(uint16_t type, uint16_t level);

// FUN_00408110: stats[0] rolls of stats[1] + rand % (stats[2] - stats[1]),
// or stats[0] * stats[2] when the two are equal.
int rollHitPoints(const agt::Template& t, Rng& rng);

struct Strike {
    bool hit = false;
    bool critical = false;  // a roll of 49 or 50: hits whatever the defence, double damage
    int roll = 0;           // 1-50
    int damage = 0;
};

// What an attack roll needs to know about the one being hit.
struct Defender {
    int defence = 0;  // template+0x10; a player's armour adds its class to it (FUN_004f0f40)
    // Damage taken per damage type in percent (template+0x204); a type not
    // listed does no damage.
    std::vector<std::pair<uint16_t, uint16_t>> resistances;
    // A player (GPlayerPlug): its armour and helmet can stop a damage type
    // (FUN_004f10e0, their levels for it) and a hit on it is never doubled.
    bool player = false;
    int armour[11] = {};
};
// An enemy's (or any template's) side of the roll.
Defender defenderOf(const agt::Template& t);

// One attack (FUN_00452290 for a blow, not a projectile): the roll
// rand % 50 + 1 plus the attacker's rating and the weapon's bonus must reach
// the target's defence; then each damage type 0-4 the weapon has, in type
// order, rolls its level's range, scaled by the target's resistance to it in
// percent and truncated. Against a player, a type whose armour level plus
// the roll exceeds the total is stopped. A hit does at least 1, and twice
// as much on a roll of 49 or 50 unless the target is a player.
// `types` limits the weapon's damage to those types (empty: all). A ranged
// attack adds the weapon's +0x2d bonus instead of +0x2c.
Strike strike(int rating, const wpn::Weapon& weapon, const Defender& target, Rng& rng,
              const std::vector<uint16_t>& types = {}, bool ranged = false);
inline Strike strike(int rating, const wpn::Weapon& weapon, const agt::Template& target, Rng& rng,
                     const std::vector<uint16_t>& types = {}) {
    return strike(rating, weapon, defenderOf(target), rng, types);
}

}  // namespace harbinger::world
