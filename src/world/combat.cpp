#include "world/combat.hpp"

#include <algorithm>

namespace harbinger::world {

namespace {

// 0x54ffb0: min, max (the third word is not used by the roll).
constexpr DamageRange kLevels[20] = {{1, 4},   {1, 6},   {3, 6},   {4, 8},   {4, 10},  {6, 12},  {6, 15},
                                     {8, 18},  {10, 20}, {15, 20}, {10, 25}, {15, 25}, {10, 30}, {15, 30},
                                     {20, 30}, {15, 40}, {20, 40}, {25, 50}, {30, 50}, {30, 60}};
// 0x550028: the effect types' (5-8) ranges.
constexpr DamageRange kEffectLevels[20] = {{1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 3},  {2, 3},  {2, 4},
                                           {2, 5}, {2, 6}, {3, 8}, {3, 10}, {5, 10}, {5, 12}, {5, 13},
                                           {6, 14}, {6, 15}, {7, 16}, {7, 17}, {8, 20}, {10, 30}};

int resistance(const Defender& t, uint16_t type) {
    for (const auto& [ty, pct] : t.resistances)
        if (ty == type) return pct;
    return 0;
}

}  // namespace

DamageRange damageRange(uint16_t type, uint16_t level) {
    if (level == 0 || level > 20) return {};
    return (type >= 5 && type <= 8) ? kEffectLevels[level - 1] : kLevels[level - 1];
}

int rollHitPoints(const agt::Template& t, Rng& rng) {
    if (t.stats.size() < 3) return 1;
    const uint32_t dice = t.stats[0], lo = t.stats[1], hi = t.stats[2];
    if (hi == lo) return int(dice * hi);
    int total = 0;
    for (uint32_t i = 0; i < dice; ++i) total += int(lo + rng.next() % uint32_t(uint16_t(hi - lo)));
    return total;
}

Defender defenderOf(const agt::Template& t) {
    Defender d;
    d.defence = t.word10;
    d.resistances = t.resistances;
    return d;
}

Strike strike(int rating, const wpn::Weapon& weapon, const Defender& target, Rng& rng,
              const std::vector<uint16_t>& types, bool ranged) {
    Strike s;
    s.roll = int(rng.next() % 50 + 1);
    const int total = rating + s.roll + (ranged ? weapon.bonus2d : weapon.hitBonus);
    s.critical = s.roll >= 49;
    if (!s.critical && total < target.defence) return s;
    bool dealt = false;
    // The roll goes through the types in order, 5-8 being effects and 9 and
    // 10 not damage.
    for (uint16_t type = 0; type < 5; ++type) {
        if (target.player && total < target.armour[type] + s.roll) continue;
        const wpn::Damage* found = weapon.find(type);
        if (!found) continue;
        const wpn::Damage& d = *found;
        if (!types.empty() && std::find(types.begin(), types.end(), d.type) == types.end()) continue;
        const DamageRange r = damageRange(d.type, d.level);
        if (r.max == 0) continue;
        const int points = r.min < r.max ? int(r.min + rng.next() % uint32_t(r.max - r.min)) : int(r.min);
        if (points <= 0) continue;
        const int pct = resistance(target, d.type);
        if (pct == 0) continue;
        s.damage += int(pct * 0.01 * points);
        dealt = true;
    }
    // Only a blow that some damage type got through counts as a hit.
    if (!dealt) return s;
    s.hit = true;
    s.damage = std::max(s.damage, 1);
    if (s.critical && !target.player) s.damage *= 2;
    return s;
}

}  // namespace harbinger::world
