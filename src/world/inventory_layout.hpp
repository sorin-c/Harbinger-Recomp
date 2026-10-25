// A character's inventory layout from its inventory screen (.itf): the grid
// size, the health pack counter, and the equipment slots in file order.
// Notes and evidence: docs/inventory.md
#pragma once

#include <string>

#include "formats/itf.hpp"
#include "world/inventory.hpp"
#include "world/player.hpp"

namespace harbinger::world {

// The character's interface folder: res\common\interface\<this>\*.itf.
std::string interfaceDir(Character c);

// The equipment slot kind of an inventory screen control, or -1 if it is
// not an equipment slot.
int equipSlotKind(uint16_t classId);

// The slots are the screen's equipment slot controls in file order; a
// socket belongs to the slot before it.
Inventory::Layout inventoryLayout(const itf::Interface& screen, Character c);

}  // namespace harbinger::world
