#include "world/actor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "world/motion.hpp"

namespace harbinger::world {

bool segmentTouchesBox(double ax, double ay, double bx, double by, double cx, double cy, double h) {
    double t0 = 0, t1 = 1;
    const double d[2] = {bx - ax, by - ay}, p[2] = {ax, ay}, lo[2] = {cx - h, cy - h}, hi[2] = {cx + h, cy + h};
    for (int i = 0; i < 2; ++i) {
        if (std::fabs(d[i]) < 1e-12) {
            if (p[i] < lo[i] || p[i] > hi[i]) return false;
            continue;
        }
        double a = (lo[i] - p[i]) / d[i], b = (hi[i] - p[i]) / d[i];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return false;
    }
    return true;
}

std::optional<WalkMap::Point> blowOffset(const std::vector<const anm::Animation*>& anims, size_t dir, uint16_t frame) {
    if (anims.empty() || !anims[0]->hotspot) return std::nullopt;
    const int refX = anims[0]->hotspot->first;
    int sum = 0, count = 0;
    std::optional<anm::Action> mine;
    for (const anm::Animation* a : anims)
        for (size_t d = 0; d < a->directions.size(); ++d)
            for (const auto& raw : a->directions[d].actions) {
                const anm::Action act = anm::action(raw);
                if (act.frame != frame || act.x < 0) continue;
                sum += std::abs(int(act.y));
                ++count;
                if (d == dir) mine = act;
            }
    if (!mine || count == 0) return std::nullopt;
    const double mean = double(sum) / count;
    const double dx = (mine->x - refX) / 128.0, dy = (std::abs(int(mine->y)) - mean) / 64.0;
    return WalkMap::Point{dy + dx, dy - dx};
}

void followPath(std::vector<WalkMap::Point>& path, double& x, double& y, uint8_t& facing, double step) {
    if (path.empty()) return;
    const WalkMap::Point target = path.front();
    const double dx = target.x - x, dy = target.y - y;
    if (dx != 0 || dy != 0) facing = facingTowards(dx, dy);
    const double mx = facingCos(facing) * step, my = facingSin(facing) * step;
    x += mx;
    y += my;
    // An axis that went past the waypoint snaps onto it; GAgentPlug_v80
    // takes the first snap as reaching the waypoint, even if the other axis
    // has not got there.
    const double restX = target.x - x, restY = target.y - y;
    bool reached = restX == 0 && restY == 0;
    const double sx = std::fabs(mx) < 1e-5 ? 0 : mx, sy = std::fabs(my) < 1e-5 ? 0 : my;
    if ((sx < 0 && restX > 0) || (sx > 0 && restX < 0)) x = target.x, reached = true;
    if ((sy < 0 && restY > 0) || (sy > 0 && restY < 0)) y = target.y, reached = true;
    if (reached) path.erase(path.begin());
}

Swing::Result Swing::advance(const std::vector<const anm::Animation*>& anims, const uint8_t& facing, uint64_t nowMs,
                             uint64_t frameMs, const std::function<bool()>& repeat) {
    Result r;
    if (anims.empty() || anims[0]->directions.empty() || anims[0]->directions[0].frames.empty()) {
        r.ended = true;
        return r;
    }
    const int64_t frames = int64_t(anims[0]->directions[0].frames.size());
    // The tags are looked up in the body's animation, which holds the
    // layers' tags too; the rebuild reads every layer's.
    for (;;) {
        const int64_t frame = int64_t((nowMs - std::min(nowMs, start_)) / frameMs);
        if (frame <= frame_) break;
        const int64_t f = frame_ + 1;
        if (f >= frames) {
            r.ended = true;
            return r;
        }
        frame_ = f;
        const size_t dir = anm::directionFor(facing, anims[0]->directions.size());
        bool looped = false;
        for (const anm::Animation* a : anims) {
            if (dir >= a->directions.size()) continue;
            for (const auto& raw : a->directions[dir].actions) {
                const anm::Action act = anm::action(raw);
                if (act.frame != f) continue;
                if (act.x == -3) {
                    // Another swing: the start time moves by y frames.
                    if (!looped && repeat()) {
                        start_ += uint64_t(-int64_t(act.y)) * frameMs;
                        frame_ += act.y;
                        looped = true;
                    }
                } else if (act.x >= 0 && !r.blow) {
                    r.blow = blowOffset(anims, dir, act.frame);
                }
            }
        }
    }
    return r;
}

}  // namespace harbinger::world
