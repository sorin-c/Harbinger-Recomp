// The level's rooms and their beacons, as the enemies' patrols use them:
// which room a map point lies in, and the beacons of the rooms linked to it.
// Notes and evidence: docs/enemies.md ("Resting"), docs/formats/level.md.
#pragma once

#include <cstdint>
#include <vector>

#include "formats/chk.hpp"
#include "world/combat.hpp"

namespace harbinger::world {

class Rooms {
public:
    // FUN_004fb860 -> FUN_004fb6e0: each room stamps its id into the half
    // cells of its rectangle (room+8..+0xe: x0, y0, width, height in half
    // cells; the file's second group of four words), later rooms over
    // earlier ones. The beacons' visit counts start at 10 + rand % 6
    // (FUN_004363b0, from the beacon constructor).
    Rooms(const chk::Level& level, Rng& rng);

    // FUN_0044b470: the room id stamped in the half cell holding (x, y), 0
    // for none.
    uint16_t at(double x, double y) const;

    // FUN_00423e40: among the beacons of the rooms linked to the room's own
    // beacon (in link id order), the one visited least, the first of equals.
    // Its count goes up by one (past 255 it restarts at 10 + rand % 6).
    // Returns its index in chk::Level::beacons, or -1.
    int leastVisitedNeighbour(uint16_t room);
    const chk::Beacon& beacon(int index) const { return level_.beacons[size_t(index)]; }

private:
    int beaconOf(uint16_t id) const;
    const chk::Level& level_;
    Rng& rng_;
    int w_ = 0, h_ = 0;
    std::vector<uint16_t> grid_;
    std::vector<uint8_t> visits_;  // beacon+0x20
};

}  // namespace harbinger::world
