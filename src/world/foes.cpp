#include "world/foes.hpp"

#include <algorithm>
#include <cmath>

#include "world/motion.hpp"

namespace harbinger::world {

namespace {

// FUN_0048b510: does the segment from (ax, ay) to (bx, by) touch the square
// of half-size h around (cx, cy)? The original clips the segment against the
// square by outcodes; this is the same test by slab clipping.
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

// The box's half-size: template+0x20 (slot 0x90) + 0.1 (0x54a308).
double halfSize(const agt::Template& t) { return t.d20 + 0.1; }

}  // namespace

Foes::Foes(Vfs& vfs, Level& level, Rng& rng) : level_(level), rng_(rng), sprites_(vfs) {
    for (const auto& a : level.map().agents) {
        if (!level.agentVisible(a)) continue;
        const agt::Template& t = level.agentTemplate(a);
        if (!isEnemyClass(t.cls)) continue;
        Foe f;
        f.record = &a;
        f.maxHitPoints = f.hitPoints = std::max(1, rollHitPoints(t, rng));
        foes_.push_back(f);
    }
}

const Foes::Foe* Foes::find(int id) const {
    return id >= 0 && size_t(id) < foes_.size() ? &foes_[size_t(id)] : nullptr;
}

int Foes::at(double x, double y) const {
    int best = -1;
    double bestD = 1e9;
    for (size_t i = 0; i < foes_.size(); ++i) {
        const Foe& f = foes_[i];
        if (f.dead) continue;
        const double h = halfSize(level_.agentTemplate(*f.record));
        const double dx = std::fabs(x - f.record->x), dy = std::fabs(y - f.record->y);
        if (dx > h || dy > h) continue;
        if (dx + dy < bestD) bestD = dx + dy, best = int(i);
    }
    return best;
}

int Foes::nearest(double x, double y, double range, std::optional<uint8_t> facing) const {
    int best = -1;
    double bestScore = 1e9;
    for (size_t i = 0; i < foes_.size(); ++i) {
        const Foe& f = foes_[i];
        if (f.dead) continue;
        const double dx = f.record->x - x, dy = f.record->y - y, d = std::hypot(dx, dy);
        if (d > range) continue;
        double score = d;
        if (facing && d > 1e-9) {
            // Up to twice as far for a foe straight ahead as for one behind.
            const double ahead = (dx * facingCos(*facing) + dy * facingSin(*facing)) / d;
            score = d * (1.5 - 0.5 * ahead);
        }
        if (score < bestScore) bestScore = score, best = int(i);
    }
    return best;
}

std::optional<Strike> Foes::blow(int id, double fromX, double fromY, double toX, double toY, int rating,
                                 const wpn::Weapon& weapon, uint64_t nowMs) {
    if (id < 0 || size_t(id) >= foes_.size()) return std::nullopt;
    Foe& f = foes_[size_t(id)];
    if (f.dead) return std::nullopt;
    const agt::Template& t = level_.agentTemplate(*f.record);
    if (!segmentTouchesBox(fromX, fromY, toX, toY, f.record->x, f.record->y, halfSize(t))) return std::nullopt;
    const Strike s = strike(rating, weapon, t, rng_, {wpn::Melee});
    if (s.hit) {
        f.hitPoints -= s.damage;
        // FUN_004a2590: damage reaching the hit points kills (FUN_00420a50).
        if (f.hitPoints <= 0) {
            f.dead = true;
            f.diedAt = nowMs;
        }
    }
    return s;
}

Level::Pose Foes::pose(const chk::AgentRecord& a, uint64_t nowMs) {
    const Foe* f = nullptr;
    for (const auto& x : foes_)
        if (x.record == &a) f = &x;
    if (!f || !f->dead) return level_.agentPose(a, nowMs);
    Level::Pose pose;
    const agt::Template& t = level_.agentTemplate(a);
    const auto die = t.animsFor(agt::Die);
    if (die.empty()) return pose;  // nothing to show: the body is gone
    const anm::Animation* anim = sprites_.anim(die[a.instance % die.size()]->path);
    if (!anim || anim->directions.empty()) return pose;
    const size_t dir = anm::directionFor(a.facing, anim->directions.size());
    if (dir >= anim->directions.size() || anim->directions[dir].frames.empty()) return pose;
    const size_t frames = anim->directions[dir].frames.size();
    const size_t frame = std::min(size_t((nowMs - std::min(nowMs, f->diedAt)) / Level::kAgentFrameMs), frames - 1);
    pose.key = "foe#" + std::to_string(a.instance) + "#die#" + std::to_string(dir) + "#" + std::to_string(frame);
    pose.image = sprites_.compose(pose.key, {anim}, dir, frame);
    if (!pose.image) return pose;
    pose.anchorX = anim->hotspot ? anim->hotspot->first : pose.image->width / 2;
    pose.anchorY = anim->hotspot ? anim->hotspot->second : pose.image->height;
    return pose;
}

}  // namespace harbinger::world
