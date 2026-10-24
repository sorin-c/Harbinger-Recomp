// Animation groups (*animgroup.mgr): the equipment layers drawn over a
// character's body (the template's own animations). Each group (an armour,
// a helmet, a weapon) names, for every agent state, an animation id in the character's
// animation index (*anim.mgr, a plain .mgr). Format notes: docs/movement.md
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace harbinger::animgroup {

struct Group {
    uint16_t id = 0;
    std::string name;               // "Armor 7", "Rifle 2", "Base Torso", ...
    std::vector<uint16_t> byState;  // *anim.mgr id per agent state (agt::State), 0 = none
};

struct Index {
    std::string name;           // the index's own name: "Human", "Gladiator", "Culibine"
    std::vector<Group> groups;  // file order
    const Group* find(const std::string& name) const;
};

// As a plain .mgr (u32 n, n x u16 id, n x str name with the NUL counted,
// str index name), then u16 states and n x states x u16. Throws std::runtime_error on malformed data or trailing bytes.
Index parse(const std::vector<uint8_t>& data);

}  // namespace harbinger::animgroup
