#include "world/walkmap.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <queue>

namespace harbinger::world {

namespace {

// FUN_00447470: which quarter of a cell each of its half cells is, as a bit
// that an edge tile of a non-walkable floor may cover. A half cell stays
// walkable unless the tile group's mask (0x57a7d8, indexed by group) sets
// its bit: (subMask | groupMask) != 0xf. Half cells are visited in the order
// (0,0), (1,0), (0,1), (1,1) (offsets at 0x57a5f8 / 0x57a600) with the masks
// at 0x57a7d8[{0xb, 0xc, 0xa, 0x9}].
constexpr uint16_t kGroupMask[16] = {0x0f, 0x04, 0x0c, 0x08, 0x09, 0x01, 0x03, 0x02,
                                     0x06, 0x0b, 0x07, 0x0e, 0x0d, 0x00, 0x00, 0x00};
constexpr int kSubDx[4] = {0, 1, 0, 1}, kSubDy[4] = {0, 0, 1, 1};
constexpr uint16_t kSubMask[4] = {0x0e, 0x0d, 0x07, 0x0b};

// FUN_00448df0's three rings around a half cell (counts at 0x57a834, offsets
// at 0x57a83c, 0x57a84c, 0x57a87c): the 2x2, 4x4 and 6x6 squares whose
// lower-right inner corner is the half cell.
struct Ring {
    int from, to;  // offsets in [from, to] on both axes, excluding the inner ring
};
constexpr Ring kRings[3] = {{-1, 0}, {-2, 1}, {-3, 2}};

// Path finder costs (0x578590, by direction: even = straight, odd =
// diagonal) and the search budget: FUN_004239f0 passes 25 and FUN_00401170
// multiplies it by the diagonal cost, so no node more than 175 away is
// expanded (FUN_00401810).
constexpr int kStraightCost = 5, kDiagonalCost = 7, kBudget = 25 * kDiagonalCost;
// Directions in FUN_00401810's numbering (0x5785a0 / 0x5785b0).
constexpr int kDirX[8] = {0, 1, 1, 1, 0, -1, -1, -1}, kDirY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

int halfCell(double v) { return int(std::floor(v * 2.0)); }

}  // namespace

WalkMap::WalkMap(int width, int height) : w_(width), h_(height), cells_(size_t(width) * size_t(height)) {}

WalkMap::WalkMap(const Level& level) : WalkMap(level.map().width * 2, level.map().height * 2) {
    const auto& m = level.map();
    for (int x = 0; x < m.width; ++x)
        for (int y = 0; y < m.height; ++y) paintFloor(level, x, y);
    // FUN_0044a7c0 for each placed gizmo: its mask's half cells.
    for (const auto& g : m.gizmos) paintGizmo(level, g, Box{0, 0, w_ - 1, h_ - 1});
    computeClearance();
}

void WalkMap::paintFloor(const Level& level, int x, int y) {
    const chk::Cell& c = level.map().at(x, y);
    if (c.count == 0) {  // no tile: nothing to stand on
        for (int k = 0; k < 4; ++k) setWalkable(2 * x + kSubDx[k], 2 * y + kSubDy[k], false);
        return;
    }
    // FUN_00448080 hands FUN_00447470 the top layer's brush and group.
    const chk::Layer& top = c.layers[c.count - 1];
    const brs::Brush* b = level.brush(top.brush);
    if (!b) {
        for (int k = 0; k < 4; ++k) setWalkable(2 * x + kSubDx[k], 2 * y + kSubDy[k], false);
        return;
    }
    // FUN_0043fac0: the group the brush registered the tile under,
    // 0xe when it has no such tile.
    const uint16_t group = brs::find(*b, top.tile) ? uint16_t(top.tile >> 12) : uint16_t(0xe);
    paintCell(x, y, *b, group);
}

void WalkMap::paintGizmo(const Level& level, const chk::GizmoRecord& g, const Box& box) {
    // From the half cell at the cell's left corner (2x, 2y + 1).
    // FUN_004b5ca0 files cells of kind 0xd in another list, which this pass
    // does not visit, and swaps the last two words: the file's `a` is the
    // in-memory word 4 (bit 0 set = blocked) and `b` word 3 (the height).
    if (!level.map().keeps(g)) return;
    const giz::Template& t = level.gizmoTemplate(g);
    if (t.masks.empty() || t.frames.empty()) return;
    // The mask of the frame the gizmo shows: a shut door blocks, an open
    // one does not.
    for (const auto& mc : t.masks[level.gizmoMask(g)].cells) {
        if (mc.kind == 0xd) continue;
        const int sx = 2 * g.x + mc.dx, sy = 2 * g.y + 1 + mc.dy;
        if (!inside(sx, sy) || sx < box.x0 || sy < box.y0 || sx > box.x1 || sy > box.y1) continue;
        setWalkable(sx, sy, !(mc.a & 1));
        // FUN_00447740(.., height, 1): the height, unless it is 0 on a
        // half cell already marked -1.
        HalfCell& hc = at(sx, sy);
        if (mc.b != 0 || hc.height != -1) hc.height = int8_t(mc.b);
    }
}

void WalkMap::repaint(const Level& level, const chk::GizmoRecord& changed) {
    // The original repaints a gizmo's half cells when its frame's mask
    // changes. Here: every map cell under any of its masks gets its floor
    // back, every gizmo is painted again inside that area (in level order,
    // as at load), and clearance is redone within 3 half cells of it.
    const giz::Template& t = level.gizmoTemplate(changed);
    Box box{w_, h_, -1, -1};
    for (const auto& m : t.masks)
        for (const auto& c : m.cells) {
            const int sx = 2 * changed.x + c.dx, sy = 2 * changed.y + 1 + c.dy;
            box.x0 = std::min(box.x0, sx & ~1), box.y0 = std::min(box.y0, sy & ~1);
            box.x1 = std::max(box.x1, sx | 1), box.y1 = std::max(box.y1, sy | 1);
        }
    box.x0 = std::max(box.x0, 0), box.y0 = std::max(box.y0, 0);
    box.x1 = std::min(box.x1, w_ - 1), box.y1 = std::min(box.y1, h_ - 1);
    if (box.x0 > box.x1 || box.y0 > box.y1) return;
    for (int x = box.x0 / 2; x <= box.x1 / 2; ++x)
        for (int y = box.y0 / 2; y <= box.y1 / 2; ++y) paintFloor(level, x, y);
    for (const auto& g : level.map().gizmos) paintGizmo(level, g, box);
    computeClearance(Box{box.x0 - 3, box.y0 - 3, box.x1 + 3, box.y1 + 3});
}

void WalkMap::setWalkable(int sx, int sy, bool walkable) {
    // FUN_004475f0: byte 1 = 0 when walkable, 0xff when not.
    if (inside(sx, sy)) at(sx, sy).clearance = walkable ? 0 : kBlocked;
}

void WalkMap::paintCell(int x, int y, const brs::Brush& b, uint16_t group) {
    const int sx = 2 * x, sy = 2 * y;
    if (b.walkable == 1 || (b.walkable == 2 && group == 0)) {
        for (int k = 0; k < 4; ++k) setWalkable(sx + kSubDx[k], sy + kSubDy[k], true);
    } else if (b.kind == 0 && b.walkable != 2) {
        // The edge of a hole or drop: the tile covers only part of the cell.
        const uint16_t gm = kGroupMask[group & 0xf];
        for (int k = 0; k < 4; ++k) setWalkable(sx + kSubDx[k], sy + kSubDy[k], (kSubMask[k] | gm) != 0xf);
    } else {
        for (int k = 0; k < 4; ++k) setWalkable(sx + kSubDx[k], sy + kSubDy[k], false);
    }
    for (int k = 0; k < 4; ++k) at(sx + kSubDx[k], sy + kSubDy[k]).height = int8_t(b.height * 2);
}

void WalkMap::computeClearance() { computeClearance(Box{0, 0, w_ - 1, h_ - 1}); }

void WalkMap::computeClearance(const Box& box) {
    for (int sx = std::max(box.x0, 0); sx <= std::min(box.x1, w_ - 1); ++sx)
        for (int sy = std::max(box.y0, 0); sy <= std::min(box.y1, h_ - 1); ++sy) {
            HalfCell& hc = at(sx, sy);
            if (hc.clearance == kBlocked) continue;
            uint8_t value = 0;
            for (int r = 0; r < 3 && !value; ++r) {
                const Ring& ring = kRings[r];
                for (int dx = ring.from; dx <= ring.to && !value; ++dx)
                    for (int dy = ring.from; dy <= ring.to; ++dy) {
                        if (r > 0 && dx > kRings[r - 1].from - 1 && dx < kRings[r - 1].to + 1 && dy > kRings[r - 1].from - 1 &&
                            dy < kRings[r - 1].to + 1)
                            continue;  // inner ring, already checked
                        const int x = sx + dx, y = sy + dy;
                        if (inside(x, y) && at(x, y).height > 0) {
                            value = uint8_t(2 - r);
                            break;
                        }
                    }
            }
            hc.clearance = value;
        }
}

bool WalkMap::passableAt(double x, double y, uint8_t limit) const {
    return x >= 0 && y >= 0 && passable(halfCell(x), halfCell(y), limit);
}

bool WalkMap::lineClear(int ax, int ay, int bx, int by, uint8_t limit, std::optional<std::pair<int, int>>* last) const {
    if (last) last->reset();
    const int dx = bx - ax, dy = by - ay;
    const bool alongX = std::abs(dy) < std::abs(dx);
    const int steps = alongX ? std::abs(dx) : std::abs(dy);
    for (int i = 1; i <= steps; ++i) {
        int x, y;
        if (alongX) {
            x = ax + (dx > 0 ? i : -i);
            y = ay + int(std::floor(double(dy) * i / steps + 0.5));
        } else {
            y = ay + (dy > 0 ? i : -i);
            x = ax + int(std::floor(double(dx) * i / steps + 0.5));
        }
        if (!passable(x, y, limit)) return false;
        if (last) *last = std::make_pair(x, y);
    }
    return true;
}

std::vector<WalkMap::Point> WalkMap::findPath(Point from, Point to, uint8_t limit) const {
    const int ax = halfCell(from.x), ay = halfCell(from.y);
    const int bx = halfCell(to.x), by = halfCell(to.y);
    if (!inside(ax, ay) || !inside(bx, by)) return {};
    // FUN_00423c30: a clear straight line needs no search.
    std::optional<std::pair<int, int>> reach;
    if (lineClear(ax, ay, bx, by, limit, &reach)) return {to};
    // Stopping short on the straight line, where the search fails (the
    // target is blocked or out of budget): half the last reachable half
    // cell, as FUN_00423c30 scales it by 0.5.
    auto fallback = [&]() -> std::vector<Point> {
        if (!reach) return {};
        return {{reach->first * 0.5, reach->second * 0.5}};
    };
    if (!passable(bx, by, limit)) return fallback();
    if (ax == bx && ay == by) return {to};

    // A* (FUN_00401170, FUN_00401810, FUN_00401af0): f = g + 49 * squared
    // distance to the target in half cells; straight steps cost 5, diagonal
    // ones 7 and need both neighbouring straight steps open.
    struct Node {
        int g = -1;
        int parent = -1;
        bool closed = false;
    };
    std::vector<Node> nodes(size_t(w_) * size_t(h_));
    auto index = [&](int x, int y) { return x * h_ + y; };
    auto heuristic = [&](int x, int y) { return 49 * ((bx - x) * (bx - x) + (by - y) * (by - y)); };
    using Entry = std::pair<int, int>;  // f, node
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    nodes[size_t(index(ax, ay))].g = 0;
    open.push({heuristic(ax, ay), index(ax, ay)});
    int found = -1;
    while (!open.empty()) {
        const auto [f, n] = open.top();
        open.pop();
        Node& node = nodes[size_t(n)];
        if (node.closed) continue;
        node.closed = true;
        const int x = n / h_, y = n % h_;
        if (x == bx && y == by) {
            found = n;
            break;
        }
        if (node.g >= kBudget) break;  // FUN_00401810 gives up
        bool open4[8] = {};
        for (int d = 0; d < 8; d += 2) open4[d] = passable(x + kDirX[d], y + kDirY[d], limit);
        for (int d = 0; d < 8; ++d) {
            const bool diagonal = d & 1;
            if (diagonal ? !(open4[d - 1] && open4[(d + 1) & 7]) : !open4[d]) continue;
            const int nx = x + kDirX[d], ny = y + kDirY[d];
            if (diagonal && !passable(nx, ny, limit)) continue;
            Node& next = nodes[size_t(index(nx, ny))];
            const int g = node.g + (diagonal ? kDiagonalCost : kStraightCost);
            if (next.closed || (next.g >= 0 && next.g <= g)) continue;
            next.g = g;
            next.parent = n;
            open.push({g + heuristic(nx, ny), index(nx, ny)});
        }
    }
    if (found < 0) return fallback();

    // Back from the target, dropping the start; FUN_004016c0 then keeps
    // only the half cells where the direction changes, and each one becomes
    // its centre (FUN_0041eb30: s * 0.5 + 0.25). The walk ends on the exact
    // point clicked.
    std::vector<std::pair<int, int>> cells;
    for (int n = found; n >= 0 && n != index(ax, ay); n = nodes[size_t(n)].parent) cells.emplace_back(n / h_, n % h_);
    std::reverse(cells.begin(), cells.end());
    std::vector<Point> out;
    auto dir = [](std::pair<int, int> a, std::pair<int, int> b) { return std::make_pair(b.first - a.first, b.second - a.second); };
    std::pair<int, int> prev{ax, ay};
    for (size_t i = 0; i + 1 < cells.size(); ++i) {
        if (dir(prev, cells[i]) != dir(cells[i], cells[i + 1])) out.push_back({cells[i].first * 0.5 + 0.25, cells[i].second * 0.5 + 0.25});
        prev = cells[i];
    }
    out.push_back(to);
    return out;
}

}  // namespace harbinger::world
