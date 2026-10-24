#include "formats/itf.hpp"

#include <array>

#include "formats/binary.hpp"

namespace harbinger::itf {
namespace {

// Per class: engine name and the fields its load method (vtable slot 5) reads.
// Layout codes: H = u16, I = u32, T = TextBox block (8 x u16), S = string.
struct ClassInfo {
    const char* name;
    const char* layout;
};

constexpr std::array<ClassInfo, 0x57> kClasses = {{
    {"Backgd", ""},                       // 0x00
    {"HealthBtn", ""},                    // 0x01
    {"InjectorBtn", ""},                  // 0x02
    {"Button", "IS"},                     // 0x03
    {"ThreeStateBtn", "IS"},              // 0x04
    {"DropItemBtn", ""},                  // 0x05
    {"HealthBar", ""},                    // 0x06
    {"EnergyBar", ""},                    // 0x07
    {"WarningLight", ""},                 // 0x08
    {"GladHealthBtn", ""},                // 0x09
    {"GladHealthBar", ""},                // 0x0a
    {"ScrollButton", "IS"},               // 0x0b
    {"ScrollButton", "IS"},               // 0x0c
    {"StaticTextBox", "TS"},              // 0x0d
    {"ScanTextBox", "I"},                 // 0x0e
    {"BlinkBtn", "IS"},                   // 0x0f
    {"InventoryGrid", ""},                // 0x10
    {"EquipSlot", "IHH"},                 // 0x11
    {"EquipSlot", "IHH"},                 // 0x12
    {"EquipSlot", "IHH"},                 // 0x13
    {"EquipSlot", "IHH"},                 // 0x14
    {"EquipSlot", "IHH"},                 // 0x15
    {"EquipSlot", "IHH"},                 // 0x16
    {"HumDropItemEquipSlot", "IHH"},      // 0x17
    {"EquipSlot", "IHH"},                 // 0x18
    {"EquipSlot", "IHH"},                 // 0x19
    {"EquipSlot", "IHH"},                 // 0x1a
    {"EquipSlot", "IHH"},                 // 0x1b
    {"EquipSlot", "IHH"},                 // 0x1c
    {"EquipSlot", "IHH"},                 // 0x1d
    {"EquipSlot", "IHH"},                 // 0x1e
    {"EquipSlot", "IHH"},                 // 0x1f
    {"EquipSlot", "IHH"},                 // 0x20
    {"EquipSlot", "IHH"},                 // 0x21
    {"EquipSlot", "IHH"},                 // 0x22
    {"EquipSlot", "IHH"},                 // 0x23
    {"InvThreeStateBtn", "IS"},           // 0x24
    {"EquipSlot", "IHH"},                 // 0x25
    {"InventoryEquipMask", ""},           // 0x26
    {"CulHealthBtn", ""},                 // 0x27
    {"CulHealthBar", ""},                 // 0x28
    {"CulAmplifierBtn", ""},              // 0x29
    {"EnergyBar", ""},                    // 0x2a
    {"EnemyHealthBar", ""},               // 0x2b
    {"CameraHealthBar", ""},              // 0x2c
    {"GladiatorToggleBtn", ""},           // 0x2d
    {"StaticTextBox", "TS"},              // 0x2e
    {"StaticTextBox", "TS"},              // 0x2f
    {"GladDropItemEquipSlot", "IHH"},     // 0x30
    {"GladDropItemBtn", ""},              // 0x31
    {"WeaponModBtn", ""},                 // 0x32
    {"Map", ""},                          // 0x33
    {"TradeTabBtn", ""},                  // 0x34
    {"TradeGrid", "HH"},                  // 0x35
    {"TradePrevPageBtn", "IS"},           // 0x36
    {"TradeNextPageBtn", "IS"},           // 0x37
    {"TradeCloseBtn", "IS"},              // 0x38
    {"PictureSlot", "I"},                 // 0x39
    {"TextBtn", "IIS"},                   // 0x3a
    {"KeyField", "IIS"},                  // 0x3b
    {"CheckBox", "IS"},                   // 0x3c
    {"SliderBar", "IS"},                  // 0x3d
    {"StatsName", "TS"},                  // 0x3e
    {"StatsSkillName", "TS"},             // 0x3f
    {"StatsSkillName", "TS"},             // 0x40
    {"StatsSkillName", "TS"},             // 0x41
    {"StatsSkillName", "TS"},             // 0x42
    {"StatsSkillValue", "TS"},            // 0x43
    {"StatsSkillValue", "TS"},            // 0x44
    {"StatsSkillValue", "TS"},            // 0x45
    {"StatsSkillValue", "TS"},            // 0x46
    {"StatsLevel", "TS"},                 // 0x47
    {"StatsBonus", "TS"},                 // 0x48
    {"PlayerIcon", "H"},                  // 0x49
    {"StatsNextLevel", ""},               // 0x4a
    {"StatsThreeStateBtn", "IS"},         // 0x4b
    {"StatsPlusBtn", "IS"},               // 0x4c
    {"StatsPlusBtn", "IS"},               // 0x4d
    {"StatsPlusBtn", "IS"},               // 0x4e
    {"StatsPlusBtn", "IS"},               // 0x4f
    {"AnimatedBtn", "HIS"},               // 0x50
    {"Icon", "H"},                        // 0x51
    {"StashGrid", "HH"},                  // 0x52
    {"StashPrevPageBtn", "IS"},           // 0x53
    {"StashNextPageBtn", "IS"},           // 0x54
    {"TradeCloseBtn", "IS"},              // 0x55
    {"QuestLogButton", "IS"},             // 0x56
}};

}  // namespace

const char* className(uint16_t classId) {
    return classId < kClasses.size() ? kClasses[classId].name : nullptr;
}

Interface parse(const std::vector<uint8_t>& data) {
    ByteReader r(data, "itf");
    Interface itf;
    for (auto& h : itf.header) h = r.i32();
    const uint16_t count = r.u16();
    itf.kind = r.u16();
    itf.controls.resize(count);
    for (auto& c : itf.controls) {
        c.classId = r.u16();
        if (c.classId >= kClasses.size()) r.fail("unknown control class");
        c.x = r.i16();
        c.y = r.i16();
        c.z = r.i16();
        c.name = r.string();
        const uint32_t images = r.u32();
        if (images > r.remaining() / 5) r.fail("implausible image count");
        for (uint32_t i = 0; i < images; ++i) c.images.push_back(r.string());
        c.extra = r.string();
        for (const char* f = kClasses[c.classId].layout; *f; ++f) {
            switch (*f) {
                case 'H': c.values.push_back(r.u16()); break;
                case 'I': c.values.push_back(r.u32()); break;
                case 'S': c.text = r.string(); break;
                case 'T': {
                    TextBox t;
                    t.width = r.u16();
                    t.height = r.u16();
                    t.font = r.u16();
                    t.fontAlt = r.u16();
                    t.fontStar = r.u16();
                    t.align = r.u16();
                    t.padX = r.u16();
                    t.padY = r.u16();
                    c.textBox = t;
                    break;
                }
            }
        }
    }
    // Sound table: only present after at least one control, and only when the flag is 1.
    if (count > 0 && r.u8() == 1) {
        const uint16_t n = r.u16();
        for (uint16_t i = 0; i < n; ++i) itf.sounds.push_back(r.u16());
    }
    r.expectEnd();
    return itf;
}

}  // namespace harbinger::itf
