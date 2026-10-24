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
    : walk_(walk), sprites_(vfs) {
    const std::string section = characterSection(character);
    template_ = agt::parse(readOrThrow(vfs, ini.require(section, "Agent")));
    animIndex_ = mgr::parse(readOrThrow(vfs, ini.require(section, "Anims")));
    groups_ = animgroup::parse(readOrThrow(vfs, ini.require(section, "AnimGroups")));
    // Each equipped item hands its animation group to one of three layers
    // (GEquipCell_v1 -> FUN_004efcf0), drawn over the body in layer order.
    const auto items = mgr::parse(readOrThrow(vfs, ini.require("Paths", "Items")));
    const auto weapons = mgr::parse(readOrThrow(vfs, ini.require("Paths", "Weapons")));
    std::string slots[3];
    int weaponRank = -1;
    for (uint16_t id : startingItems(character)) {
        const std::string* path = items.find(id);
        if (!path) continue;
        const auto data = readOrThrow(vfs, *path);
        const uint16_t cls = data.size() < 2 ? 0 : uint16_t(data[0] | data[1] << 8);
        if (cls < itm::HumanWeapon || cls > itm::RightGauntlet) continue;  // mod chips: in a socket
        const itm::Item item = itm::parse(data);
        // The blows' weapon: the melee one where there is a choice (the
        // gladiator's blade over its gun, the right gauntlet over the left).
        // The original's choice (FUN_004213a0) is not traced: guessed.
        static constexpr uint16_t kRank[] = {0, 3, 0, 0, 1, 4, 0, 1, 2};
        if (kRank[cls] > weaponRank) {
            if (const std::string* w = weapons.find(item.socketed->word38)) {
                weapon_ = wpn::parse(readOrThrow(vfs, *w));
                weaponRank = kRank[cls];
            }
        }
        const int layer = itm::spriteLayer(item.classId);
        if (layer < 0) continue;
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
    target_ = -1;
    path_ = walk_.findPath({x_, y_}, {x, y}, limit_);
    return !path_.empty();
}

void Player::steer(double dx, double dy) {
    steerX_ = dx;
    steerY_ = dy;
    if (dx != 0 || dy != 0) path_.clear(), target_ = -1;
}

void Player::attack(int id, double x, double y) {
    steerX_ = steerY_ = 0;
    if (target_ == id && state_ == agt::Melee) return;  // already swinging at it
    target_ = id;
    targetX_ = x;
    targetY_ = y;
    targetAlive_ = true;
    path_.clear();
    if (std::hypot(x - x_, y - y_) > kReach) path_ = walk_.findPath({x_, y_}, {x, y}, limit_);
}

void Player::trackTarget(double x, double y, bool alive) {
    targetX_ = x;
    targetY_ = y;
    targetAlive_ = alive;
}

void Player::faceTarget() {
    // GAgentPlug_v58: an attack turns the attacker to its target.
    if (targetX_ != x_ || targetY_ != y_) facing_ = facingTowards(targetX_ - x_, targetY_ - y_);
}

void Player::setState(agt::State s, uint64_t nowMs) {
    if (s == state_) return;
    state_ = s;
    stateStart_ = nowMs;  // a new state starts its animation from the first frame
}

std::optional<Player::Blow> Player::update(uint64_t nowMs) {
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
    // A new order (walking, steering) cuts the swing short.
    if (state_ == agt::Melee && target_ < 0 && (steerX_ != 0 || steerY_ != 0 || !path_.empty())) setState(agt::Idle, nowMs);
    if (state_ == agt::Melee) return stepMelee(nowMs);
    if (target_ >= 0) {
        if (!targetAlive_) {
            target_ = -1;
            path_.clear();
        } else if (std::hypot(targetX_ - x_, targetY_ - y_) <= kReach) {
            path_.clear();
            faceTarget();
            setState(agt::Melee, nowMs);
            meleeFrame_ = -1;
            return stepMelee(nowMs);
        } else if (path_.empty()) {
            target_ = -1;  // nowhere left to walk: give up
        }
    }
    if (steerX_ != 0 || steerY_ != 0) stepSteer(step);
    else if (!path_.empty()) stepPath(step);
    setState((steerX_ != 0 || steerY_ != 0 || !path_.empty()) ? agt::Move : agt::Idle, nowMs);
    return std::nullopt;
}

std::optional<Player::Blow> Player::stepMelee(uint64_t nowMs) {
    const auto anims = layerAnims(agt::Melee);
    if (anims.empty() || anims[0]->directions.empty() || anims[0]->directions[0].frames.empty()) {
        setState(agt::Idle, nowMs);
        return std::nullopt;
    }
    const int64_t frames = int64_t(anims[0]->directions[0].frames.size());
    std::optional<Blow> blow;
    // Run the tags of every frame reached since the last step (GAgentPlug_v9
    // looks them up in the body's animation, which holds the layers' tags too).
    for (;;) {
        const int64_t frame = int64_t((nowMs - std::min(nowMs, stateStart_)) / kFrameMs);
        if (frame <= meleeFrame_) break;
        const int64_t f = meleeFrame_ + 1;
        if (f >= frames) {
            setState(agt::Idle, nowMs);  // the swing is over
            if (!targetAlive_) target_ = -1;
            return blow;
        }
        meleeFrame_ = f;
        const size_t dir = anm::directionFor(facing_, anims[0]->directions.size());
        bool looped = false;
        for (const anm::Animation* a : anims) {
            if (dir >= a->directions.size()) continue;
            for (const auto& raw : a->directions[dir].actions) {
                const anm::Action act = anm::action(raw);
                if (act.frame != f) continue;
                if (act.x == -3) {
                    // Another swing while the target can be hit: jump y
                    // frames (start time moves by y frames) and face it again.
                    if (target_ >= 0 && targetAlive_ && std::hypot(targetX_ - x_, targetY_ - y_) <= kReach + 0.25) {
                        stateStart_ += uint64_t(-int64_t(act.y)) * kFrameMs;
                        meleeFrame_ += act.y;
                        faceTarget();
                        looped = true;
                    }
                } else if (act.x >= 0 && target_ >= 0) {
                    if (auto off = blowOffset(dir, act.frame)) blow = Blow{target_, x_ + off->x, y_ + off->y};
                }
            }
        }
        if (looped) continue;
    }
    return blow;
}

std::optional<WalkMap::Point> Player::blowOffset(size_t dir, uint16_t frame) {
    // FUN_0042a6a0: the tags of one frame, across all directions, circle the
    // agent; their mean |y| is the circle's centre row. A tag's offset from
    // it, (dx, dy) = ((x - hotspot x) / 128, (|y| - mean) / 64), is a screen
    // offset turned into the map: (dy + dx, dy - dx).
    const auto anims = layerAnims(agt::Melee);
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
    for (const auto& p : paths)
        if (const anm::Animation* a = sprites_.anim(p)) out.push_back(a);
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
    const size_t frame = size_t((nowMs - std::min(nowMs, stateStart_)) / kFrameMs % frames);
    pose.key = "player#" + std::to_string(int(state_)) + "#" + std::to_string(dir) + "#" + std::to_string(frame);
    pose.image = sprites_.compose(pose.key, anims, dir, frame);
    if (!pose.image) return pose;
    pose.anchorX = base.hotspot ? base.hotspot->first : pose.image->width / 2;
    pose.anchorY = base.hotspot ? base.hotspot->second : pose.image->height;
    return pose;
}

}  // namespace harbinger::world
