#include "world/inventory_layout.hpp"

namespace harbinger::world {

namespace {

// Socket kinds: a slot of one of these belongs to the slot before it.
bool socketKind(int kind) {
    switch (kind) {
        case 2: case 4: case 6: case 0xA: case 0xC: case 0xE: case 0x12: return true;
        default: return false;
    }
}

}  // namespace

std::string interfaceDir(Character c) {
    switch (c) {
        case Character::Gladiator: return "gladiator";
        case Character::Culibine: return "culibine";
        default: return "human";
    }
}

int equipSlotKind(uint16_t classId) {
    // GCtrlEquipSlot's class id less 0x10 is its kind (0x11 armour ... 0x23
    // the Culibine's amplifiers); the two drop item slots are classes of
    // their own and take the drop kinds FUN_004cd130 gives them.
    if (classId == 0x17) return 0x16;  // HumDropItemEquipSlot
    if (classId == 0x30) return 7;     // GladDropItemEquipSlot
    if (classId >= 0x11 && classId <= 0x23) return classId - 0x10;
    return -1;
}

Inventory::Layout inventoryLayout(const itf::Interface& screen, Character c) {
    Inventory::Layout l;
    // FUN_004ceaa0: the Gladiator's grid has six rows, the others five.
    l.gridW = 12;
    l.gridH = c == Character::Gladiator ? 6 : 5;
    // FUN_004cd070 / FUN_004ee5d0: five packs of ten, the Culibine one of one.
    if (c == Character::Culibine) l.healthPacks = 1, l.maxHealthPacks = 1;
    int parent = -1;
    for (const auto& ctl : screen.controls) {
        const int kind = equipSlotKind(ctl.classId);
        if (kind < 0) continue;
        const bool socket = socketKind(kind);
        l.slots.push_back({uint16_t(kind), socket ? parent : -1});
        if (!socket) parent = int(l.slots.size()) - 1;
    }
    return l;
}

}  // namespace harbinger::world
