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

// One attack (FUN_00452290 for a blow, not a projectile): the roll
// rand % 50 + 1 plus the attacker's rating and the weapon's bonus must reach
// the target's defence (template+0x10); then each damage type the weapon
// has, other than 5-10, rolls its level's range, scaled by the target's
// resistance to it in percent and truncated. A hit does at least 1.
// `types` limits the weapon's damage to those types (empty: all).
Strike strike(int rating, const wpn::Weapon& weapon, const agt::Template& target, Rng& rng,
              const std::vector<uint16_t>& types = {});

}  // namespace harbinger::world
