#include "world/rooms.hpp"

#include <algorithm>
#include <cmath>

namespace harbinger::world {

Rooms::Rooms(const chk::Level& level, Rng& rng)
    : level_(level), rng_(rng), w_(level.width * 2), h_(level.height * 2), grid_(size_t(w_) * size_t(h_), 0) {
    for (const auto& r : level.rooms) {
        const int x0 = r.b[0], y0 = r.b[1];
        for (int y = y0; y < y0 + r.b[3]; ++y)
            for (int x = x0; x < x0 + r.b[2]; ++x)
                if (x < w_ && y < h_) grid_[size_t(x) * size_t(h_) + size_t(y)] = r.id;
    }
    visits_.reserve(level.beacons.size());
    for (size_t i = 0; i < level.beacons.size(); ++i) visits_.push_back(uint8_t(rng_.next() % 6 + 10));
}

uint16_t Rooms::at(double x, double y) const {
    const int sx = int(std::floor(x * 2)), sy = int(std::floor(y * 2));
    if (sx < 0 || sy < 0 || sx >= w_ || sy >= h_) return 0;
    return grid_[size_t(sx) * size_t(h_) + size_t(sy)];
}

int Rooms::beaconOf(uint16_t id) const {
    // FUN_00436b30: the beacon with the room's id (one beacon per room).
    for (size_t i = 0; i < level_.beacons.size(); ++i)
        if (level_.beacons[i].room == id) return int(i);
    return -1;
}

int Rooms::leastVisitedNeighbour(uint16_t room) {
    const int own = room ? beaconOf(room) : -1;
    if (own < 0) return -1;
    // The links are a map keyed by the linked id, so they are visited in
    // ascending id order; a later beacon wins only with a lower count.
    auto links = level_.beacons[size_t(own)].links;
    std::sort(links.begin(), links.end());
    int best = -1;
    for (const auto& [id, cost] : links) {
        const int b = beaconOf(id);
        if (b < 0) continue;
        if (best < 0 || visits_[size_t(b)] < visits_[size_t(best)]) best = b;
    }
    if (best < 0) return -1;
    uint8_t& v = visits_[size_t(best)];
    v = v == 0xff ? uint8_t(rng_.next() % 6 + 10) : uint8_t(v + 1);
    return best;
}

}  // namespace harbinger::world
