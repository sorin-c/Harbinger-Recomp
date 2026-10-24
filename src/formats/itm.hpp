// Item templates (.itm). Decoded so far: the common header, the animated
// item (front-end buttons) and the socketed equipment classes 1-8 (weapons,
// armour, helmets, gauntlets).
// Format notes and evidence: docs/formats/itm.md
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace harbinger::itm {

// Frame marks of an animated item (GAnimatedItem). See docs/formats/itm.md for
// how the engine steps through them on hover and click.
struct Animation {
    uint16_t loopStart = 0;  // hover loops over [loopStart, loopEnd)
    uint16_t loopEnd = 0;
    uint16_t pressEnd = 0;   // a click plays on from loopEnd and rests here
};

// The body shared by the equipment classes (GSocketedItem_v3, 0x503290) and
// what the armour and weapon loaders add after it.
struct Socketed {
    uint16_t word38 = 0;     // item+0x38
    uint8_t byte3c = 0;      // item+0x3c
    uint16_t animGroup = 0;  // item+0x3e: the wearer's *animgroup.mgr id (GEquipCell_v1)
    std::optional<uint16_t> word54;  // armour from version 1.1 (GArmorItem_v3, item+0x54)
    // From version 2.1 (FUN_0048ba50): a word and a list of word pairs.
    uint16_t listWord = 0;
    std::vector<std::pair<uint16_t, uint16_t>> list;
};

struct Item {
    uint16_t classId = 0;  // index into the engine's item factory (25 = GAnimatedItem)
    uint16_t versionWord = 0;  // the version times 256 (0x233 = 2.2 in most files)
    uint16_t unknown2 = 0;
    uint16_t image = 0;       // itemimg.mgr id
    uint16_t frameCount = 0;  // frames stacked vertically in the image
    uint16_t unknown5 = 0, unknown6 = 0;
    uint32_t unknown7 = 0;
    std::string name;
    std::optional<std::vector<uint8_t>> block;  // the optional 8 bytes (FUN_004d6fc0)
    std::optional<Animation> animation;  // set for class 25
    std::optional<Socketed> socketed;    // set for classes 1-8
    uint16_t trailer = 0;  // u16 read last by the base loader from version 1.2; 11 in every animated item

    double version() const { return versionWord / 256.0; }
};

// Factory indices (the creators registered at 0x584570 + 4 * index).
enum : uint16_t {
    HumanWeapon = 1,        // GHumanWeaponItem
    HumanArmor = 2,         // GHumanArmorItem
    HumanHelmet = 3,        // GHumanHelmetItem
    GladiatorRanged = 4,    // GGladiatorRangedWeaponItem
    GladiatorMelee = 5,     // GGladiatorMeleeWeaponItem
    GladiatorTorso = 6,     // GGladiatorTorsoItem
    LeftGauntlet = 7,       // GLeftGauntletItem
    RightGauntlet = 8,      // GRightGauntletItem
    AnimatedItem = 25,      // GAnimatedItem
};

// The sprite layer an equipped item of this class draws on (GEquipCell_v1
// hands its animation group to layer 0, 1 or 2), or -1 for none.
int spriteLayer(uint16_t classId);

// Throws std::runtime_error on malformed data, trailing bytes, or a class whose
// body is not decoded yet.
Item parse(const std::vector<uint8_t>& data);

}  // namespace harbinger::itm
