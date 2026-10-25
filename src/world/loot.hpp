// Loot: what chests drop when they open, the items lying on the ground, and
// picking them up. Notes and evidence: docs/formats/loot.md
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/chk.hpp"
#include "formats/gig.hpp"
#include "formats/giz.hpp"
#include "formats/itm.hpp"
#include "formats/mgr.hpp"
#include "world/combat.hpp"
#include "world/level.hpp"
#include "world/player.hpp"
#include "world/walkmap.hpp"

namespace harbinger::world {

// What a chest, barrel or trash pile holds (GBarrelPlug_v22: after the
// label, u16 item group, u16 item, u16 flags, u16 state).
struct ChestContents {
    uint16_t group = 0;  // plug+0xde: an id in the character's item group index, 0 = none
    uint16_t item = 0;   // plug+0xe0: an item.mgr id that always drops, 0 = none
    uint16_t flags = 0;  // bit 0 -> plug+0xdc, bit 1 -> plug+0xdd (with no group: the level's default group, always none)
};
std::optional<ChestContents> chestContents(const giz::Template& t, const chk::GizmoRecord& g);

// One dropped item; `amount` is a money item's value (GMoneyPlug+0x38).
struct Drop {
    uint16_t item = 0;
    uint32_t amount = 0;
};

// FUN_004d8dd0's roll: countMin..countMax picks from the group by weight
// (an item id of 0 ends the drop), a money amount for each money item, and
// the chest's own item unless the group already dropped it.
std::vector<Drop> rollDrops(const gig::Group* group, uint16_t guaranteed, const std::function<bool(uint16_t)>& isMoney,
                            Rng& rng);

class Loot {
public:
    Loot(Vfs& vfs, const Ini& ini, Character character, Level& level, const WalkMap& walk, Rng& rng);

    struct GroundItem {
        uint32_t id = 0;  // unique for this level
        Drop drop;
        double x = 0, y = 0;   // map cells
        uint64_t showMs = 0;   // when it lands
        const itm::Item* item = nullptr;  // header; nullptr if the item file is missing
    };

    // Once a frame, after the level's gizmos have updated: each chest that
    // has just finished opening (state 2 -> 3, GBarrelPlug_v29) drops its
    // contents around its use point, one item every kDropMs.
    void update(uint64_t nowMs);
    const std::vector<GroundItem>& ground() const { return ground_; }
    // Ground items that have landed by `nowMs`.
    bool landed(const GroundItem& g, uint64_t nowMs) const { return nowMs >= g.showMs; }
    const GroundItem* find(uint32_t id) const;
    // The nearest landed item within `range` cells of (x, y), or nullptr.
    const GroundItem* nearest(double x, double y, double range, uint64_t nowMs) const;
    // Takes an item off the ground; returns it, or nothing if it is gone.
    std::optional<Drop> take(uint32_t id);

    // Lays one item on the ground at map point (x, y), landing at once (a
    // dialogue's gift the inventory has no room for).
    void place(const Drop& d, double x, double y, uint64_t nowMs);

    const itm::Item* item(uint16_t id);
    static constexpr uint64_t kDropMs = 500;

private:
    void drop(const chk::GizmoRecord& g, const ChestContents& c, uint64_t nowMs);

    Vfs& vfs_;
    Level& level_;
    const WalkMap& walk_;
    Rng& rng_;
    mgr::Index groups_, items_;
    std::map<uint16_t, std::optional<gig::Group>> groupCache_;
    std::map<uint16_t, std::optional<itm::Item>> itemCache_;
    std::map<uint16_t, uint16_t> chestStates_;  // instance -> state last frame
    std::vector<GroundItem> ground_;
    uint32_t nextId_ = 1;
};

}  // namespace harbinger::world
