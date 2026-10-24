// Item groups (.gig): the loot tables chests and enemies drop from, listed in
// each character's item group index ([Human|Gladiator|Culibine] ItemGroups).
// Format notes and evidence: docs/formats/loot.md
#pragma once

#include <cstdint>
#include <vector>

namespace harbinger::gig {

struct Entry {
    uint16_t item = 0;    // item.mgr id
    uint16_t weight = 0;  // chance, out of the group's total weight
};

// GItemGroup (FUN_004d6ec0, entries FUN_00441c30).
struct Group {
    uint8_t moneyMin = 1, moneyMax = 2;  // group+0x14, +0x15: amount range of a money item
    uint8_t countMin = 0, countMax = 1;  // group+0x16, +0x17: how many items drop
    std::vector<Entry> entries;
    uint32_t totalWeight() const;
};

// Throws std::runtime_error on short or trailing data.
Group parse(const std::vector<uint8_t>& data);

}  // namespace harbinger::gig
