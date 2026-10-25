// The player's inventory (GInventory): the grid of carried items, the
// equipment slots and the mod chip sockets of the equipped items, money, the
// health pack counter, and the item held on the pointer while the inventory
// screen rearranges things. Free of drawing; the screen is app/hud.
// Notes and evidence: docs/inventory.md
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "formats/itm.hpp"

namespace harbinger::world {

// What the inventory needs to know about an item template.
struct ItemInfo {
    uint16_t id = 0;
    uint16_t classId = 0;
    std::string name;
    uint8_t w = 1, h = 1;  // grid cells (template+0xe, +0x10: the header's words 5 and 6)
    // The cells it covers when the header's block is present: bit x of byte
    // y (FUN_004d5a10); without one, the whole w x h rectangle.
    std::optional<std::array<uint8_t, 8>> mask;
    uint16_t kind = 0;    // the equipment slot kind it goes in (itm::equipKind), 0 = none
    uint8_t sockets = 0;  // mod chip sockets of a socketed item (Socketed::byte3c)
    bool money = false;   // GMoneyItem: adds its amount to the money
    bool healthPack = false;  // GHealthItem (classes 14-16): counted, not placed

    bool covers(int x, int y) const;
};
ItemInfo describe(uint16_t id, const itm::Item& item);

// One item the player has. A socketed item keeps the ids of the chips in its
// sockets (the u16 list at plug+0x38 that FUN_004cbfb0 fills), so they travel
// with it when it is unequipped.
struct Carried {
    uint16_t id = 0;
    std::vector<uint16_t> chips;  // socket order; 0 = empty socket
};

// An equipment slot of the inventory screen. `parent` is the slot whose item
// owns this socket (-1 for a slot of its own).
struct SlotDef {
    uint16_t kind = 0;
    int parent = -1;
};

class Inventory {
public:
    using Lookup = std::function<const ItemInfo*(uint16_t)>;

    struct Layout {
        int gridW = 12, gridH = 5;  // FUN_004ceaa0 in the character's slot 108
        std::vector<SlotDef> slots;
        uint16_t healthPacks = 5, maxHealthPacks = 10;  // FUN_004cd070 / FUN_004ee5d0
    };
    Inventory(Layout layout, Lookup lookup);

    // Picking something up (FUN_004ccd10): money adds up; a health pack goes
    // on the counter while it has room; equipment goes in the first empty
    // slot that takes it, a chip in the first empty socket that takes it;
    // anything else in the first place in the grid that fits, scanning rows
    // from the top left (FUN_004ceef0).
    enum class Added { Money, HealthPack, Equipped, Socketed, Grid, NoRoom };
    Added add(const Carried& item, uint32_t amount = 0);

    int gridW() const { return layout_.gridW; }
    int gridH() const { return layout_.gridH; }
    struct Placed {
        Carried item;
        int x = 0, y = 0;  // top-left cell
    };
    const std::vector<Placed>& grid() const { return grid_; }
    // The grid item covering cell (x, y), or -1.
    int itemAt(int x, int y) const;
    // Whether `id` can lie with its top-left cell at (x, y): Free, or Swap
    // when it covers exactly one other item (its index in `swap`)
    // (FUN_004cf100, FUN_004cfd90).
    enum class Fit { No, Free, Swap };
    Fit fit(uint16_t id, int x, int y, int* swap = nullptr) const;
    // The top-left cell an item held at pointer cell (x, y) goes to: centred
    // on the pointer and kept inside the grid (FUN_004cf4e0).
    std::pair<int, int> dropCell(uint16_t id, int x, int y) const;
    bool autoPlace(const Carried& item);

    // Equipment. A socket slot shows (and takes chips) only while its
    // parent holds an item with at least that many sockets.
    size_t slotCount() const { return layout_.slots.size(); }
    const SlotDef& slot(size_t i) const { return layout_.slots.at(i); }
    bool slotEnabled(size_t i) const;
    // The item in a slot (for a socket, a Carried with the chip's id), or nothing.
    std::optional<Carried> equipped(size_t i) const;
    bool accepts(size_t i, uint16_t id) const;
    // Armour, weapons and gauntlets cannot be taken off, only swapped for
    // another (FUN_004d5f30 locks those cells).
    static bool locked(uint16_t kind);
    // The ids of the equipped items, in slot order (no chips).
    std::vector<uint16_t> equippedIds() const;
    // The chips in an equipped item's sockets.
    std::vector<uint16_t> chipsOf(size_t slot) const;

    // The item on the pointer (DAT_0058437c+0x584a4).
    const std::optional<Carried>& held() const { return held_; }
    // A click on grid cell (x, y) (GCtrlInventoryGrid_v7): with nothing held,
    // take the item there; holding one, put it down there, picking up the
    // one it displaces. A click on an equipment slot (GCtrlEquipSlot_v7):
    // take, put or swap the same way. Each returns what happened.
    enum class Click { Nothing, Took, Put, Swapped };
    Click clickGrid(int x, int y);
    Click clickSlot(size_t i);
    // Puts the held item back in the grid (when the screen closes).
    bool stowHeld();

    uint32_t money() const { return money_; }
    uint16_t healthPacks() const { return packs_; }
    uint16_t maxHealthPacks() const { return layout_.maxHealthPacks; }
    // Takes one pack off the counter, which a pack carried in the grid
    // then refills; false if there is none.
    bool takeHealthPack();

    // What dialogues and quests ask of it. FUN_004cf9e0: whether an item
    // with this id lies in the grid (not the equipment slots), and with
    // `remove` takes the first one out.
    bool inGrid(uint16_t id) const;
    bool removeFromGrid(uint16_t id);
    // Money a dialogue gives, and FUN_004ee620: takes `amount` if there is
    // that much, else nothing (false).
    void addMoney(uint32_t amount);
    bool spend(uint32_t amount);
    // GiveHealth: one pack onto the counter while it has room; false if full.
    bool addHealthPack();

    // Changes whenever the equipment changes (the sprite and weapon follow it).
    uint64_t equipmentRevision() const { return equipRev_; }

private:
    const ItemInfo* info(uint16_t id) const { return lookup_(id); }
    int socketIndex(size_t i) const;  // position of socket slot i among its parent's sockets
    void put(size_t slot, Carried item);
    std::optional<Carried> take(size_t slot);

    Layout layout_;
    Lookup lookup_;
    std::vector<Placed> grid_;
    std::vector<std::optional<Carried>> slots_;  // by slot; sockets stay empty (their chips live in the parent)
    std::optional<Carried> held_;
    uint32_t money_ = 0;
    uint16_t packs_ = 0;
    uint64_t equipRev_ = 0;
};

}  // namespace harbinger::world
