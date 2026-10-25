#include "world/bolts.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "world/motion.hpp"

namespace harbinger::world {

namespace {

// FUN_004f90b0: the effects' own random numbers, a signed 32-bit draw
// scaled to [-1, 1). The rebuild draws from the game's generator.
double randSigned(Rng& rng) { return int32_t(rng.next()) / 2147483648.0; }

// FUN_0048b390 (likely): does the step from (ax, ay) to (bx, by) pass
// within r of (cx, cy)?
bool segmentNearPoint(double ax, double ay, double bx, double by, double cx, double cy, double r) {
    const double dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? ((cx - ax) * dx + (cy - ay) * dy) / len2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    const double px = ax + t * dx - cx, py = ay + t * dy - cy;
    return px * px + py * py <= r * r;
}

}  // namespace

bool lineOfFire(const WalkMap& walk, double x0, double y0, double x1, double y1, double z) {
    // The line is sampled every quarter of a half cell; a half cell counts
    // once it is entered. The original steps cell by cell (not traced in
    // detail).
    const double limit = 2.0 * z;
    const double len = std::hypot(x1 - x0, y1 - y0);
    const int steps = std::max(1, int(std::ceil(len * 8)));
    int lastX = -1, lastY = -1;
    for (int i = 0; i <= steps; ++i) {
        const double t = double(i) / steps;
        const double x = x0 + (x1 - x0) * t, y = y0 + (y1 - y0) * t;
        if (x < 0 || y < 0) return false;
        const int sx = int(std::floor(x * 2)), sy = int(std::floor(y * 2));
        if (sx == lastX && sy == lastY) continue;
        lastX = sx, lastY = sy;
        if (!walk.inside(sx, sy)) return false;
        if (limit > 0 ? walk.at(sx, sy).height > limit : walk.at(sx, sy).clearance == WalkMap::kBlocked) return false;
    }
    return true;
}

WalkMap::Point leadAim(double sx, double sy, double tx, double ty, double tvx, double tvy,
                       std::optional<WalkMap::Point> dest, double boltSpeed, int delayMs) {
    const double speed = std::hypot(tvx, tvy) * 1000.0;  // cells per second
    if (speed <= 0 || boltSpeed <= 0) return {tx, ty};
    const double dist = std::hypot(tx - sx, ty - sy);
    const uint8_t towards = facingTowards(tx - sx, ty - sy);
    const uint8_t moving = facingTowards(tvx, tvy);
    const double closing = facingCos(uint8_t(moving - towards)) * speed;
    const double t = delayMs * 0.001 + dist / (boltSpeed * 1000.0 - closing);
    WalkMap::Point aim{tx + t * facingCos(moving) * speed, ty + t * facingSin(moving) * speed};
    if (dest && std::hypot(dest->x - tx, dest->y - ty) < std::hypot(aim.x - tx, aim.y - ty)) aim = *dest;
    return aim;
}

Bolts::Bolts(Vfs& vfs, const Ini& ini, const WalkMap& walk, Rng& rng) : vfs_(vfs), walk_(walk), rng_(rng) {
    if (auto data = vfs.read(ini.require("Paths", "Effects"))) index_ = mgr::parse(*data);
}

const emt::Emitter* Bolts::effect(uint16_t id) {
    auto it = effects_.find(id);
    if (it == effects_.end()) {
        std::unique_ptr<emt::Emitter> e;
        if (const std::string* path = index_.find(id))
            if (auto data = vfs_.read(*path)) {
                try {
                    e = std::make_unique<emt::Emitter>(emt::parse(*data));
                } catch (const std::exception&) {
                }
            }
        it = effects_.emplace(id, std::move(e)).first;
    }
    const emt::Emitter* e = it->second.get();
    return e && e->complete && e->basic ? e : nullptr;
}

double Bolts::speedOf(const wpn::Weapon& weapon) {
    // FUN_00522030: the emitter's speed (GBasicDesc slot 7: +0x60 scale *
    // +0x18 speed) with the scale set from the weapon's type 10 mods,
    // 1 + 0.01 * mods, at most 2 (no mod chips yet: 1).
    if (!weapon.ranged) return 0;
    const emt::Emitter* e = effect(weapon.ranged->effect);
    return e ? e->basic->speed : 0;
}

bool Bolts::fire(int shooter, double x, double y, double z, uint8_t heading, const wpn::Weapon& weapon, int rating,
                 uint64_t nowMs) {
    if (!weapon.ranged) return false;
    const emt::Emitter* e = effect(weapon.ranged->effect);
    if (!e) return false;
    const emt::Basic& b = *e->basic;
    // GBasicDesc slot 8: the fan count, or 1.
    const int count = b.fan > 0 ? b.fan : 1;
    for (int i = 0; i < count; ++i) {
        Bolt t;
        t.shooter = shooter;
        t.weapon = weapon;
        t.rating = rating;
        t.look = e;
        // GBasicDesc_v1: the azimuth spreads by +0x13 either way (a value
        // above 128 counts down from 128), at random or, with the fan flag,
        // in even steps; the polar angle from +0x10 by up to +0x12.
        int var = b.azimuthVar;
        if (var > 0x80) var = 0x80 - var;
        double az;
        if ((b.flags & 2) && b.fan >= 2) az = (b.azimuth - var) + i * (2.0 * var / (b.fan - 1));
        else az = b.azimuth + var * randSigned(rng_);
        const double polar = b.polar + b.polarVar * randSigned(rng_);
        const double speed = b.speed + b.speedVar * randSigned(rng_);
        // Angles in 256ths of a turn (0x556950 = pi / 128); the description
        // is turned to the emitter's heading (FUN_004e9150). The bolt keeps
        // its height: the weapons' bolts leave level (polar 64) and do not
        // accelerate.
        const double turn = (heading + az) * std::numbers::pi / 128.0;
        const double flat = std::sin(polar * std::numbers::pi / 128.0) * speed;
        t.vx = std::cos(turn) * flat;
        t.vy = std::sin(turn) * flat;
        t.heading = facingTowards(t.vx, t.vy);
        t.x = x + b.spread[0] * randSigned(rng_);
        t.y = y + b.spread[1] * randSigned(rng_);
        t.z = z;
        t.born = t.last = nowMs;
        t.dies = nowMs + b.lifeMs + uint64_t(std::max(0.0, b.lifeVarMs * randSigned(rng_)));
        // FUN_00520bc0: particle+0xa8 = -(the weapon's pierce + its type 12
        // mods); a hit makes it positive and takes the target's defence off.
        t.pierce = -weapon.ranged->pierce;
        bolts_.push_back(t);
    }
    return true;
}

std::vector<Bolts::Impact> Bolts::update(uint64_t nowMs,
                                         const std::function<std::vector<Body>(int shooter)>& targets) {
    std::vector<Impact> out;
    for (Bolt& t : bolts_) {
        if (nowMs <= t.last) continue;
        const uint64_t until = std::min(nowMs, t.dies);
        const double dt = double(until - t.last);
        // GBasicDesc_v2: position += velocity * ms.
        const double x0 = t.x, y0 = t.y;
        const double x1 = x0 + t.vx * dt, y1 = y0 + t.vy * dt;
        t.last = nowMs;
        const bool spent = until >= t.dies;  // its life runs out in this step
        // FUN_00499320: the step must be clear of walls at the bolt's height
        // plus 0.1 (FUN_00402120); then the hostile, live agents near it are
        // tried in turn, and the first whose circle the step crosses is hit.
        if (!lineOfFire(walk_, x0, y0, x1, y1, t.z + 0.1)) {
            // A wall: particle+0xa8 loses 25; with nothing left (or a weapon
            // without pierce) the bolt goes out. Bouncing bolts are not
            // rebuilt.
            t.dies = 0;
            continue;
        }
        t.x = x1, t.y = y1;
        if (spent) t.dies = 0;
        std::optional<Body> hit;
        double best = 1e18;
        for (const Body& body : targets(t.shooter)) {
            if (!segmentNearPoint(x0, y0, x1, y1, body.x, body.y, body.radius)) continue;
            const double d = (body.x - x0) * (body.x - x0) + (body.y - y0) * (body.y - y0);
            if (d < best) best = d, hit = body;
        }
        if (!hit) continue;
        Impact im;
        im.shooter = t.shooter;
        im.target = hit->id;
        im.x = t.x, im.y = t.y;
        im.sound = t.weapon.ranged ? t.weapon.ranged->impactSound : 0;
        // FUN_00452290 with the ranged weapon: its +0x2d bonus, and from a
        // weapon that also strikes in melee (kind 3) only types 1-4
        // (GRangedWeapon_v5, FUN_00520060).
        static const std::vector<uint16_t> kRangedTypes = {1, 2, 3, 4};
        if (hit->defender)
            im.strike = strike(t.rating, t.weapon, *hit->defender, rng_,
                               t.weapon.kind == 3 ? kRangedTypes : std::vector<uint16_t>{}, true);
        out.push_back(im);
        // The bolt's pierce: made positive, less the target's defence; at 0
        // or below it goes out.
        if (t.pierce < 1) t.pierce = -t.pierce;
        t.pierce -= hit->defender ? hit->defender->defence : 0;
        if (t.pierce < 1) t.dies = 0;
    }
    bolts_.erase(std::remove_if(bolts_.begin(), bolts_.end(), [](const Bolt& t) { return t.dies == 0; }),
                 bolts_.end());
    return out;
}

}  // namespace harbinger::world
