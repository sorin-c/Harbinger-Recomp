#include "world/player.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "formats/itm.hpp"
#include "world/motion.hpp"

namespace harbinger::world {

namespace {

std::vector<uint8_t> readOrThrow(Vfs& vfs, const std::string& path) {
    auto data = vfs.read(path);
    if (!data) throw std::runtime_error("missing resource: " + path);
    return std::move(*data);
}

// The items each character starts with, added one by one when a game starts
// (vtable slot 108 of the character's plug: GHumanPlayerPlug_v108 0x4ee6b0,
// GGladPlayerPlug_v108 0x4badd0, GCulibinePlug_v108 0x470b10), as item.mgr
// ids. Each is equipped (seen in the original's inventory for the Human:
// Alpha Advance Armor, the E-16 Rifle and an EMP mod chip in its socket).
std::vector<uint16_t> startingItems(Character c) {
    switch (c) {
        case Character::Human: return {689, 333, 74};             // armour, rifle, EMP chip
        case Character::Gladiator: return {251, 291, 747, 103};   // blade, gun, torso, EMP chip
        case Character::Culibine: return {40, 1081, 48};          // left and right gauntlets, EMP chip
    }
    return {};
}

// Source-over for agent layers, as the compositor FUN_004c2710 stacks them:
// art (opaque) replaces what is below; a marker pixel (shadow, translucent)
// never covers art and keeps the darker of the two shadows.
void stack(render::RgbaImage& dst, const std::vector<uint32_t>& src, int w, int h) {
    for (int v = 0; v < std::min(h, dst.height); ++v)
        for (int u = 0; u < std::min(w, dst.width); ++u) {
            const uint32_t p = src[size_t(v) * size_t(w) + size_t(u)];
            const uint32_t a = p >> 24;
            if (!a) continue;
            uint32_t& d = dst.pixels[size_t(v) * size_t(dst.width) + size_t(u)];
            if (a == 255) d = p;
            else if ((d >> 24) < a) d = p;
        }
}

}  // namespace

const char* characterSection(Character c) {
    switch (c) {
        case Character::Human: return "Human";
        case Character::Gladiator: return "Gladiator";
        case Character::Culibine: return "Culibine";
    }
    return "Human";
}

Player::Player(Vfs& vfs, const Ini& ini, Character character, const WalkMap& walk, const chk::EntryPoint& start)
    : vfs_(vfs), walk_(walk) {
    const std::string section = characterSection(character);
    template_ = agt::parse(readOrThrow(vfs, ini.require(section, "Agent")));
    animIndex_ = mgr::parse(readOrThrow(vfs, ini.require(section, "Anims")));
    groups_ = animgroup::parse(readOrThrow(vfs, ini.require(section, "AnimGroups")));
    // Each equipped item hands its animation group to one of three layers
    // (GEquipCell_v1 -> FUN_004efcf0), drawn over the body in layer order.
    const auto items = mgr::parse(readOrThrow(vfs, ini.require("Paths", "Items")));
    std::string slots[3];
    for (uint16_t id : startingItems(character)) {
        const std::string* path = items.find(id);
        if (!path) continue;
        const auto data = readOrThrow(vfs, *path);
        // Mod chips go into the weapon's socket and draw nothing.
        if (data.size() < 2 || itm::spriteLayer(uint16_t(data[0] | data[1] << 8)) < 0) continue;
        const itm::Item item = itm::parse(data);
        const int layer = itm::spriteLayer(item.classId);
        for (const auto& g : groups_.groups)
            if (g.id == item.socketed->animGroup) slots[layer] = g.name;
    }
    for (const auto& name : slots)
        if (!name.empty()) layers_.push_back(name);
    // The move animation's parameter is the walking speed (FUN_00406100).
    for (const auto& a : template_.anims)
        if (a.kind == agt::Move && !speed_) speed_ = walkSpeed(a.param);
    // Stat 6 is template+0x50 (FUN_00406a60), the size GAgentPlug_v31 hands
    // the path finder.
    const int size = template_.stats.size() > 6 ? template_.stats[6] : 1;
    limit_ = WalkMap::limitFor(std::clamp(size, 0, 2));
    x_ = start.x;
    y_ = start.y;
    facing_ = uint8_t(start.facing & 0xff);
}

bool Player::walkTo(double x, double y) {
    steerX_ = steerY_ = 0;
    path_ = walk_.findPath({x_, y_}, {x, y}, limit_);
    return !path_.empty();
}

void Player::steer(double dx, double dy) {
    steerX_ = dx;
    steerY_ = dy;
    if (dx != 0 || dy != 0) path_.clear();
}

void Player::setState(agt::State s, uint64_t nowMs) {
    if (s == state_) return;
    state_ = s;
    stateStart_ = nowMs;  // a new state starts its animation from the first frame
}

void Player::update(uint64_t nowMs) {
    if (!started_) {
        started_ = true;
        lastMs_ = stateStart_ = nowMs;
    }
    const uint64_t dt = nowMs - lastMs_;
    lastMs_ = nowMs;
    // GAgentPlug_v80: the distance covered is speed * 0.01 * (half the last
    // step's time + half this one's), in milliseconds.
    const double halfDt = double(dt) * 0.5;
    const double step = speed_ * (halfLastDt_ + halfDt);
    halfLastDt_ = halfDt;
    if (steerX_ != 0 || steerY_ != 0) stepSteer(step);
    else if (!path_.empty()) stepPath(step);
    setState((steerX_ != 0 || steerY_ != 0 || !path_.empty()) ? agt::Move : agt::Idle, nowMs);
}

void Player::stepPath(double step) {
    const WalkMap::Point target = path_.front();
    const double dx = target.x - x_, dy = target.y - y_;
    if (dx != 0 || dy != 0) facing_ = facingTowards(dx, dy);
    const double mx = facingCos(facing_) * step, my = facingSin(facing_) * step;
    x_ += mx;
    y_ += my;
    // FUN_0041e200: an axis that went past the waypoint snaps onto it (moves
    // under 0.00001 count as none). GAgentPlug_v80 takes the first snap as
    // reaching the waypoint, even if the other axis has not got there.
    const double restX = target.x - x_, restY = target.y - y_;
    bool reached = restX == 0 && restY == 0;
    const double sx = std::fabs(mx) < 1e-5 ? 0 : mx, sy = std::fabs(my) < 1e-5 ? 0 : my;
    if ((sx < 0 && restX > 0) || (sx > 0 && restX < 0)) x_ = target.x, reached = true;
    if ((sy < 0 && restY > 0) || (sy > 0 && restY < 0)) y_ = target.y, reached = true;
    if (reached) path_.erase(path_.begin());
}

void Player::stepSteer(double step) {
    facing_ = facingTowards(steerX_, steerY_);
    const double mx = facingCos(facing_) * step, my = facingSin(facing_) * step;
    // Walk where the half cell is open to the player; against a wall, keep
    // the part of the move along it.
    if (walk_.passableAt(x_ + mx, y_ + my, limit_)) {
        x_ += mx;
        y_ += my;
    } else if (std::fabs(mx) > 1e-9 && walk_.passableAt(x_ + mx, y_, limit_)) {
        x_ += mx;
    } else if (std::fabs(my) > 1e-9 && walk_.passableAt(x_, y_ + my, limit_)) {
        y_ += my;
    }
}

const gim::Image* Player::sheet(const std::string& path) {
    auto it = sheets_.find(path);
    if (it == sheets_.end()) {
        std::unique_ptr<gim::Image> img;
        if (auto data = vfs_.read(path)) img = std::make_unique<gim::Image>(gim::decode(*data));
        it = sheets_.emplace(path, std::move(img)).first;
    }
    return it->second.get();
}

std::vector<const anm::Animation*> Player::layerAnims(agt::State s) {
    // The body is the template's own animation for the state (the player
    // templates have one per state), then each equipment layer's.
    std::vector<std::string> paths;
    const auto own = template_.animsFor(s);
    if (own.empty()) return {};
    paths.push_back(own[0]->path);
    for (const auto& name : layers_) {
        const animgroup::Group* g = groups_.find(name);
        if (!g || size_t(s) >= g->byState.size() || !g->byState[size_t(s)]) continue;
        if (const std::string* p = animIndex_.find(g->byState[size_t(s)])) paths.push_back(*p);
    }
    std::vector<const anm::Animation*> out;
    for (const auto& p : paths) {
        auto it = anims_.find(p);
        if (it == anims_.end()) {
            std::unique_ptr<anm::Animation> a;
            if (auto data = vfs_.read(p)) a = std::make_unique<anm::Animation>(anm::parse(*data));
            it = anims_.emplace(p, std::move(a)).first;
        }
        if (it->second) out.push_back(it->second.get());
    }
    return out;
}

Player::Pose Player::pose(uint64_t nowMs) {
    Pose pose;
    const auto anims = layerAnims(state_);
    if (anims.empty()) return pose;
    const anm::Animation& base = *anims[0];
    const size_t dir = anm::directionFor(facing_, base.directions.size());
    // FUN_0042c020: 100 ms a frame, looping over the first direction's count.
    const size_t frames = base.directions.empty() ? 0 : base.directions[0].frames.size();
    if (frames == 0 || dir >= base.directions.size() || base.directions[dir].frames.empty()) return pose;
    const size_t frame = size_t((nowMs - std::min(nowMs, stateStart_)) / 100 % frames);
    const auto key = std::make_tuple(int(state_), dir, frame);
    auto it = poses_.find(key);
    if (it == poses_.end()) {
        render::RgbaImage img;
        for (const anm::Animation* a : anims) {
            if (dir >= a->directions.size() || a->directions[dir].frames.empty()) continue;
            const gim::Image* s = sheet(anm::sheetPath(a->directions[dir].frames[0]));
            if (!s || s->frames.empty()) continue;
            if (img.pixels.empty()) {
                img.width = s->width;
                img.height = s->height;
                img.pixels.assign(size_t(img.width) * size_t(img.height), 0);
            }
            // Every layer's sheet has the same size and lines up with the
            // body's (checked against the original's sprite).
            stack(img, render::toRgba(*s, std::min(frame, s->frames.size() - 1), render::KeyMode::Agent), s->width,
                  s->height);
        }
        it = poses_.emplace(key, std::move(img)).first;
    }
    if (it->second.pixels.empty()) return pose;
    pose.key = "player#" + std::to_string(int(state_)) + "#" + std::to_string(dir) + "#" + std::to_string(frame);
    pose.image = &it->second;
    pose.anchorX = base.hotspot ? base.hotspot->first : it->second.width / 2;
    pose.anchorY = base.hotspot ? base.hotspot->second : it->second.height;
    return pose;
}

}  // namespace harbinger::world
