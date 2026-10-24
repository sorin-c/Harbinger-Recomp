// Weapon definitions (.wpn, indexed by [Paths] Weapons = weapons.mgr): the
// damage an item or an agent deals, by damage type and level, and an armour's
// armour class. Items name theirs by id (itm::Socketed::word38), agents by
// stats 3 (ranged) and 4 (melee).
// Format notes and evidence: docs/combat.md
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace harbinger::wpn {

// Damage types, as the damage roll (FUN_00452290) and the resistances use
// them. 5-8 are effects rather than hit points; 9 is an armour's class.
enum DamageType : uint16_t { Melee = 0, Plasma = 1, Emp = 2, ArmourClass = 9 };

struct Damage {
    uint16_t type = 0;
    uint16_t level = 0;  // row of the damage table (1-based), or the armour class for type 9
    uint16_t extra = 0;  // the third word (FUN_005202b0 keeps it with the level)
};

struct Weapon {
    double version = 0;
    uint16_t kind = 0;      // 0 melee, 1-3 ranged, 4 armour
    uint8_t hitBonus = 0;   // weapon+0x2c, added to the attack roll (from version 1.1)
    uint8_t bonus2d = 0;    // weapon+0x2d
    std::vector<Damage> damage;
    std::vector<uint8_t> rest;  // the ranged kinds' 26 or 28 more bytes, undecoded

    const Damage* find(uint16_t type) const {
        for (const auto& d : damage)
            if (d.type == type) return &d;
        return nullptr;
    }
};

// Throws std::runtime_error on malformed data.
Weapon parse(const std::vector<uint8_t>& data);

}  // namespace harbinger::wpn
