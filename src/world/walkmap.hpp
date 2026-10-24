// Where agents can stand and walk: the level's half-cell grid and the path
// finder that runs on it. Format and code notes: docs/movement.md
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "world/level.hpp"

namespace harbinger::world {

// The chunk's walk grid (chunk+0xe4, allocated in FUN_00447000 area): every
// map cell is split into 2x2 half cells, so the grid is (2 * width) by
// (2 * height). Half cell (sx, sy) covers map x in [sx/2, sx/2 + 0.5) and y in
// [sy/2, sy/2 + 0.5); a map point lies in half cell (floor(2x), floor(2y)).
class WalkMap {
public:
    // A half cell's two bytes (the original keeps 8 per half cell).
    struct HalfCell {
        int8_t height = 0;      // byte 0: wall height * 2 (brush), or a gizmo mask cell's height
        uint8_t clearance = 0;  // byte 1: 0xff = blocked; else 0, 1, 2 = near a tall half cell (FUN_00448df0)
    };
    static constexpr uint8_t kBlocked = 0xff;

    // Builds the grid as the chunk load does: each cell's top layer
    // (FUN_00448080 -> FUN_00447470), then every placed gizmo's footprint
    // (FUN_0044a7c0), then the clearance pass (FUN_00448df0).
    explicit WalkMap(const Level& level);
    // An empty grid of the given size (tests).
    WalkMap(int width, int height);

    int width() const { return w_; }   // half cells
    int height() const { return h_; }
    bool inside(int sx, int sy) const { return sx >= 0 && sy >= 0 && sx < w_ && sy < h_; }
    HalfCell& at(int sx, int sy) { return cells_[size_t(sx) * size_t(h_) + size_t(sy)]; }
    const HalfCell& at(int sx, int sy) const { return cells_[size_t(sx) * size_t(h_) + size_t(sy)]; }

    // The clearance an agent of `size` (template stat 6, GAgentPlug_v31) needs:
    // FUN_00401cb0 and FUN_00401170 let it onto a half cell whose clearance
    // byte is below 3 - size, so the player (size 1) keeps one half cell away
    // from walls and tall gizmos.
    static uint8_t limitFor(int size) { return uint8_t(3 - size); }
    bool passable(int sx, int sy, uint8_t limit) const { return inside(sx, sy) && at(sx, sy).clearance < limit; }
    bool passableAt(double x, double y, uint8_t limit) const;

    // Recomputes the clearance bytes of every walkable half cell
    // (FUN_00448df0 over the whole map).
    void computeClearance();

    struct Point {
        double x, y;
    };
    // A path from `from` to `to` in map cells (GMoveAction via FUN_004239f0 ->
    // FUN_00423c30): the waypoints to walk through in order, the last one
    // being where the walk ends. Empty when the agent cannot move at all.
    std::vector<Point> findPath(Point from, Point to, uint8_t limit) const;

    // The straight-line test FUN_00401cb0: steps along the longer axis from
    // half cell (ax, ay) to (bx, by). True if every half cell on the way is
    // passable; `last` is the last passable half cell reached either way
    // (unset when the first step is already blocked).
    bool lineClear(int ax, int ay, int bx, int by, uint8_t limit, std::optional<std::pair<int, int>>* last = nullptr) const;

private:
    void setWalkable(int sx, int sy, bool walkable);
    void paintCell(int x, int y, const brs::Brush& brush, uint16_t group);

    int w_ = 0, h_ = 0;
    std::vector<HalfCell> cells_;
};

}  // namespace harbinger::world
