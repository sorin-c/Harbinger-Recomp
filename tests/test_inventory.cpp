// Tests for the inventory: grid placement with item shapes, swapping, the
// pick-up order (money, health packs, equipment, sockets, grid), the locked
// slots and chips travelling with their item. Synthetic cases always run;
// with HARBINGER_DATA set, every mod chip and injector parses and the
// Human's starting items end up where the original shows them.
#include <cstdio>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/itm.hpp"
#include "formats/mgr.hpp"
#include "world/inventory.hpp"
#include "world/player.hpp"

using namespace harbinger;
using world::Carried;
using world::Inventory;
using world::ItemInfo;

static int failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

namespace {

// Synthetic items: 1 armour (kind 1, 2x3), 2 rifle (kind 5, 6x2, 1 socket),
// 3 weapon chip (kind 6), 4 helmet (kind 3, 2x2), 5 money, 6 health pack,
// 7 an L-shaped 2x2 item (cells (0,0), (0,1), (1,1)), 8 a 1x1 item,
// 9 a second rifle with 2 sockets.
std::map<uint16_t, ItemInfo> items() {
    std::map<uint16_t, ItemInfo> m;
    auto add = [&](uint16_t id, uint8_t w, uint8_t h, uint16_t kind) {
        ItemInfo i;
        i.id = id, i.w = w, i.h = h, i.kind = kind;
        m[id] = i;
        return &m[id];
    };
    add(1, 2, 3, 1);
    add(2, 6, 2, 5)->sockets = 1;
    add(3, 1, 1, 6);
    add(4, 2, 2, 3);
    add(5, 2, 2, 0)->money = true;
    add(6, 1, 1, 0)->healthPack = true;
    add(7, 2, 2, 0)->mask = std::array<uint8_t, 8>{0b01, 0b11, 0, 0, 0, 0, 0, 0};
    add(8, 1, 1, 0);
    add(9, 6, 2, 5)->sockets = 2;
    return m;
}

// The Human's slots, as human/inventory.itf lists them (a socket belongs to
// the slot before it).
Inventory::Layout humanLayout() {
    Inventory::Layout l;
    l.slots = {{1, -1}, {2, 0}, {2, 0}, {3, -1}, {4, 3}, {5, -1}, {6, 5}, {6, 5}, {8, -1}};
    return l;
}

void testShapes() {
    const auto m = items();
    const ItemInfo& l = m.at(7);
    CHECK(l.covers(0, 0) && !l.covers(1, 0) && l.covers(0, 1) && l.covers(1, 1));
    CHECK(m.at(2).covers(5, 1));  // no mask: the whole rectangle
}

void testGrid() {
    const auto m = items();
    Inventory inv(humanLayout(), [&](uint16_t id) { return m.count(id) ? &m.at(id) : nullptr; });
    // Rows from the top left (FUN_004ceef0).
    CHECK(inv.autoPlace({8, {}}) && inv.grid().back().x == 0 && inv.grid().back().y == 0);
    CHECK(inv.autoPlace({8, {}}) && inv.grid().back().x == 1 && inv.grid().back().y == 0);
    // The L shape leaves its empty corner free: a 1x1 item fits there.
    Inventory shapes(humanLayout(), [&](uint16_t id) { return m.count(id) ? &m.at(id) : nullptr; });
    CHECK(shapes.autoPlace({7, {}}));
    CHECK(shapes.itemAt(1, 0) < 0 && shapes.itemAt(0, 0) == 0 && shapes.itemAt(1, 1) == 0);
    CHECK(shapes.fit(8, 1, 0) == Inventory::Fit::Free);
    CHECK(shapes.autoPlace({8, {}}) && shapes.grid().back().x == 1 && shapes.grid().back().y == 0);
    // Outside the grid, over two items, over one item.
    CHECK(inv.fit(2, 7, 0) == Inventory::Fit::No);
    CHECK(inv.fit(4, 0, 0) == Inventory::Fit::No);
    int swap = -1;
    CHECK(inv.fit(4, 1, 0, &swap) == Inventory::Fit::Swap && swap == 1);
    // The held item is centred on the pointer cell and kept in the grid (FUN_004cf4e0).
    CHECK(inv.dropCell(2, 0, 0) == std::make_pair(0, 0));
    CHECK(inv.dropCell(2, 11, 4) == std::make_pair(6, 3));
    CHECK(inv.dropCell(4, 5, 3) == std::make_pair(4, 2));
}

void testClicks() {
    const auto m = items();
    Inventory inv(humanLayout(), [&](uint16_t id) { return m.count(id) ? &m.at(id) : nullptr; });
    inv.autoPlace({8, {}});
    inv.autoPlace({4, {}});  // at (1, 0)
    // Take the helmet, put it down over the 1x1 item: they swap.
    CHECK(inv.clickGrid(2, 1) == Inventory::Click::Took && inv.held() && inv.held()->id == 4);
    CHECK(inv.clickGrid(0, 0) == Inventory::Click::Swapped && inv.held()->id == 8);
    CHECK(inv.grid().size() == 1 && inv.grid()[0].x == 0 && inv.grid()[0].y == 0);
    CHECK(inv.clickGrid(5, 3) == Inventory::Click::Put && !inv.held());
    // The helmet slot takes a helmet and gives it back (not locked).
    inv.clickGrid(0, 0);
    CHECK(inv.clickSlot(0) == Inventory::Click::Nothing);  // not armour
    CHECK(inv.clickSlot(3) == Inventory::Click::Put && inv.equipped(3)->id == 4);
    CHECK(inv.clickSlot(3) == Inventory::Click::Took && !inv.equipped(3));
    CHECK(inv.stowHeld() && !inv.held());
}

void testAdd() {
    const auto m = items();
    Inventory inv(humanLayout(), [&](uint16_t id) { return m.count(id) ? &m.at(id) : nullptr; });
    CHECK(inv.healthPacks() == 5);
    CHECK(inv.add({5, {}}, 120) == Inventory::Added::Money && inv.money() == 120);
    // Starting items one by one (GHumanPlayerPlug_v108): armour, rifle, chip.
    CHECK(inv.add({1, {}}) == Inventory::Added::Equipped && inv.equipped(0)->id == 1);
    // No weapon yet: the chip's socket is not there, so it goes in the grid.
    Inventory early(humanLayout(), [&](uint16_t id) { return m.count(id) ? &m.at(id) : nullptr; });
    CHECK(early.add({3, {}}) == Inventory::Added::Grid);
    CHECK(inv.add({2, {}}) == Inventory::Added::Equipped && inv.equipped(5)->id == 2);
    CHECK(inv.slotEnabled(6) && !inv.slotEnabled(7));
    CHECK(inv.add({3, {}}) == Inventory::Added::Socketed && inv.equipped(6)->id == 3);
    CHECK(inv.chipsOf(5) == std::vector<uint16_t>{3});
    // A second chip: its socket is full and the second one is not there.
    CHECK(inv.add({3, {}}) == Inventory::Added::Grid);
    // Five more packs fill the counter to ten; the eleventh is carried.
    for (int i = 0; i < 5; ++i) CHECK(inv.add({6, {}}) == Inventory::Added::HealthPack);
    CHECK(inv.healthPacks() == 10 && inv.add({6, {}}) == Inventory::Added::Grid);
    CHECK(inv.takeHealthPack() && inv.healthPacks() == 9);
    // Armour and weapons cannot be taken off, only swapped.
    CHECK(inv.clickSlot(0) == Inventory::Click::Nothing && inv.clickSlot(5) == Inventory::Click::Nothing);
    // Swap the rifle for the two-socket one: the old rifle keeps its chip.
    const uint64_t rev = inv.equipmentRevision();
    Inventory::Added a = inv.add({9, {}});
    CHECK(a == Inventory::Added::Grid);
    const auto& p = inv.grid().back();
    CHECK(inv.clickGrid(p.x, p.y) == Inventory::Click::Took && inv.held()->id == 9);
    CHECK(inv.clickSlot(5) == Inventory::Click::Swapped && inv.held()->id == 2);
    CHECK(inv.held()->chips == std::vector<uint16_t>{3});
    CHECK(inv.equipmentRevision() != rev);
    CHECK(inv.slotEnabled(6) && inv.slotEnabled(7) && !inv.equipped(6));
    // A chip comes out of its socket and goes back in.
    CHECK(inv.stowHeld());
    const int k = inv.itemAt(0, 0);
    CHECK(k >= 0);
}

void testRealData(const char* dir) {
    Vfs vfs(dir);
    const auto items = mgr::parse(*vfs.read("res\\common\\managers\\item.mgr"));
    // Every mod chip and injector body parses to its last byte, and each
    // chip fits a socket kind.
    int chips = 0, injectors = 0;
    std::map<uint16_t, ItemInfo> infos;
    for (const auto& [id, path] : items.entries) {
        const auto data = vfs.read(path);
        if (!data) continue;
        const auto h = itm::parseHeader(*data);
        if (h.classId != itm::ModChip && h.classId != itm::Injector && (h.classId < 1 || h.classId > 8)) continue;
        try {
            const auto it = itm::parse(*data);
            if (it.modChip) {
                ++chips;
                CHECK(itm::equipKind(it) != 0);
            }
            if (it.injector) ++injectors;
            infos[id] = world::describe(id, it);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "item %u: %s\n", id, e.what());
            ++failures;
        }
    }
    std::printf("%d mod chips, %d injectors parsed\n", chips, injectors);
    CHECK(chips == 222 && injectors == 6);
    // The E-16 Rifle: 6x2 cells, one socket; the EMP chip fits a weapon socket.
    CHECK(infos.at(333).w == 6 && infos.at(333).h == 2 && infos.at(333).sockets == 1 && infos.at(333).kind == 5);
    CHECK(infos.at(689).w == 4 && infos.at(689).h == 3 && infos.at(689).kind == 1);
    CHECK(infos.at(74).kind == 6);
    // The Human's starting items, added as GHumanPlayerPlug_v108 does: the
    // armour and rifle in their slots and the chip in the rifle's socket,
    // as the original's inventory screen shows them.
    Inventory inv(humanLayout(), [&](uint16_t id) { return infos.count(id) ? &infos.at(id) : nullptr; });
    for (uint16_t id : world::Player::startingItems(world::Character::Human)) inv.add({id, {}});
    CHECK(inv.equipped(0) && inv.equipped(0)->id == 689);
    CHECK(inv.equipped(5) && inv.equipped(5)->id == 333);
    CHECK(inv.equipped(6) && inv.equipped(6)->id == 74);
    CHECK(inv.grid().empty() && inv.money() == 0 && inv.healthPacks() == 5);
}

}  // namespace

int main() {
    testShapes();
    testGrid();
    testClicks();
    testAdd();
    if (const char* dir = std::getenv("HARBINGER_DATA"))
        testRealData(dir);
    else
        std::printf("HARBINGER_DATA not set; skipping real-data checks\n");
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
