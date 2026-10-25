// The text the inventory's description popup shows for an item, built the
// way the original's format strings lay it out ("%s   %s: Lvl. %hu (%hu -
// %hu) (+%hu)", "Damage:", "Slots: ", "AC: ", " mod chip"). Lines starting
// with '&' are drawn in the popup's second font (the original's orange).
// Notes and evidence: docs/inventory.md
#pragma once

#include <string>
#include <vector>

#include "formats/itm.hpp"
#include "formats/wpn.hpp"

namespace harbinger::world {

// The damage type names, in type order (the table at 0x54fb70: Melee,
// Plasma, EMP, Electricity, Disruption, Narcotic, Decay, Corrosion, Nerve
// Toxin, Armor Class).
const char* damageTypeName(uint16_t type);

// `weapon` is the item's .wpn (socketed items name it by Socketed::word38),
// or nullptr; `chips` the mod chips in its sockets.
std::string itemDescription(const itm::Item& item, const wpn::Weapon* weapon,
                            const std::vector<itm::Item>& chips = {});

// The health button's popup: "5 Health Packs", "1 Health Pack".
std::string healthPackText(int count);

}  // namespace harbinger::world
