#include "world/player.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "formats/itm.hpp"
#include "world/bolts.hpp"
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
    // Armour and helmet (plug+0x2d4, +0x2d8): their armour class (type 9,
    // level plus the third word) adds to the defence when worn (FUN_004f0f40,
    // FUN_004f0fb0), and their level for each damage type can stop it
    // (FUN_004f10e0). The gladiator's torso is taken as its armour (guessed).
    defender_ = defenderOf(template_);
    defender_.player = true;
    for (uint16_t id : startingItems(character)) {
        const std::string* path = items.find(id);
        if (!path) continue;
        const auto data = readOrThrow(vfs, *path);
        const uint16_t cls = data.size() < 2 ? 0 : uint16_t(data[0] | data[1] << 8);
        if (cls < itm::HumanWeapon || cls > itm::RightGauntlet) continue;  // mod chips: in a socket
        const itm::Item item = itm::parse(data);
        if (cls == itm::HumanArmor || cls == itm::HumanHelmet || cls == itm::GladiatorTorso) {
            if (const std::string* w = weapons.find(item.socketed->word38)) {
                const wpn::Weapon armour = wpn::parse(readOrThrow(vfs, *w));
                for (const auto& d : armour.damage) {
                    if (d.type == wpn::ArmourClass) defender_.defence += d.level + d.extra;
                    else if (d.type < 11) defender_.armour[d.type] += d.level;
                }
            }
        }
        // The blows' weapon: the melee one where there is a choice (the
        // gladiator's blade over its gun, the right gauntlet over the left).
        // The original's choice (FUN_004213a0) is not traced: guessed.
        static constexpr uint16_t kRank[] = {0, 3, 0, 0, 1, 4, 0, 1, 2};
        if (const std::string* w = weapons.find(item.socketed->word38)) {
            wpn::Weapon weapon = wpn::parse(readOrThrow(vfs, *w));
            // The shots' weapon: the first that fires (kinds 1-3).
            if (weapon.ranged && !ranged_) ranged_ = weapon;
            if (kRank[cls] > weaponRank) {
                weapon_ = std::move(weapon);
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
    switch (character) {
        case Character::Human: maxHitPoints_ = 70; break;
        case Character::Gladiator: maxHitPoints_ = 80; break;
        case Character::Culibine: maxHitPoints_ = 60; break;
    }
    hitPoints_ = maxHitPoints_;
    // GRangedWeapon_v7 starts the energy full (+0x58 = +0x60).
    if (ranged_) energy_ = std::max<double>(0, ranged_->ranged->maxEnergy);
}

void Player::takeDamage(int damage, uint64_t nowMs) {
    if (dead_ || damage <= 0) return;
    // GAgentPlug_v61: what is left above 0 stays (slot 0xa4); at 0 or below
    // the hit points are set to 0 and the die action starts (action 6).
    hitPoints_ -= damage;
    if (hitPoints_ > 0) return;
    hitPoints_ = 0;
    dead_ = true;
    diedAt_ = nowMs;
    visibility_ = 0;  // GAgentPlug_v73: an agent with no hit points cannot be seen
    path_.clear();
    steerX_ = steerY_ = 0;
    target_ = -1;
    state_ = agt::Idle;
    setState(agt::Die, nowMs);
}

bool Player::walkTo(double x, double y) {
    if (dead_) return false;
    steerX_ = steerY_ = 0;
    target_ = -1;
    path_ = walk_.findPath({x_, y_}, {x, y}, limit_);
    return !path_.empty();
}

void Player::steer(double dx, double dy) {
    if (dead_) return;
    steerX_ = dx;
    steerY_ = dy;
    if (dx != 0 || dy != 0) path_.clear(), target_ = -1;
}

void Player::attack(int id, double x, double y) {
    if (dead_) return;
    steerX_ = steerY_ = 0;
    if (target_ == id && state_ == agt::Melee) return;  // already swinging at it
    if (state_ == agt::Shoot) setState(agt::Idle, lastMs_);
    rangedMode_ = false;
    target_ = id;
    targetX_ = x;
    targetY_ = y;
    targetAlive_ = true;
    path_.clear();
    if (std::hypot(x - x_, y - y_) > kReach) path_ = walk_.findPath({x_, y_}, {x, y}, limit_);
}

void Player::shoot(int id, double x, double y) {
    if (dead_) return;
    if (!ranged_) return attack(id, x, y);
    steerX_ = steerY_ = 0;
    if (target_ == id && rangedMode_ && state_ == agt::Shoot) return;  // already shooting at it
    if (state_ == agt::Melee) setState(agt::Idle, lastMs_);
    target_ = id;
    rangedMode_ = true;
    targetX_ = x;
    targetY_ = y;
    targetAlive_ = true;
    targetVx_ = targetVy_ = 0;
    targetDest_.reset();
    path_.clear();
    if (!canShoot()) path_ = walk_.findPath({x_, y_}, {x, y}, limit_);
}

void Player::trackTarget(double x, double y, bool alive, double vx, double vy, std::optional<WalkMap::Point> dest) {
    targetX_ = x;
    targetY_ = y;
    targetAlive_ = alive;
    targetVx_ = vx;
    targetVy_ = vy;
    targetDest_ = dest;
}

bool Player::canShoot() const {
    // In range, and nothing tall between the player and the target at about
    // a muzzle's height (a rebuild choice for when to stop walking).
    if (std::hypot(targetX_ - x_, targetY_ - y_) > kShootRange) return false;
    return lineOfFire(walk_, x_, y_, targetX_, targetY_, kMuzzleGuess + 0.1);
}

void Player::aim(int delayMs) {
    // FUN_0041f8d0 at the -2 tag: aim at the target; if it is walking, lead
    // it by delay * 0.001 s plus the bolt's flight, dist / (bolt speed -
    // its speed towards the shooter), but no further than where its walk
    // ends.
    aimX_ = targetX_, aimY_ = targetY_;
    const double speed = std::hypot(targetVx_, targetVy_) * 1000.0;  // cells per second
    if (speed <= 0 || boltSpeed_ <= 0) return;
    const double dist = std::hypot(targetX_ - x_, targetY_ - y_);
    const uint8_t towards = facingTowards(targetX_ - x_, targetY_ - y_);
    const uint8_t moving = facingTowards(targetVx_, targetVy_);
    const double closing = facingCos(uint8_t(moving - towards)) * speed;
    const double t = delayMs * 0.001 + dist / (boltSpeed_ * 1000.0 - closing);
    double ax = targetX_ + t * facingCos(moving) * speed, ay = targetY_ + t * facingSin(moving) * speed;
    if (targetDest_) {
        const double toDest = std::hypot(targetDest_->x - targetX_, targetDest_->y - targetY_);
        if (toDest < std::hypot(ax - targetX_, ay - targetY_)) ax = targetDest_->x, ay = targetDest_->y;
    }
    aimX_ = ax, aimY_ = ay;
}

void Player::faceTarget() {
    // GAgentPlug_v58: an attack turns the attacker to its target, and makes
    // it more noticeable: +25, up to 220.
    if (targetX_ != x_ || targetY_ != y_) facing_ = facingTowards(targetX_ - x_, targetY_ - y_);
    if (visibility_ < 220) visibility_ = uint8_t(std::min(220, visibility_ + 25));
}

void Player::setState(agt::State s, uint64_t nowMs) {
    if (s == state_) return;
    state_ = s;
    stateStart_ = nowMs;  // a new state starts its animation from the first frame
    loops_ = 0;
}

void Player::noticeLoops(uint64_t nowMs) {
    // Each time the idle or walk animation comes round (FUN_0042c020
    // returning 1): standing settles the visibility 10 back towards its base
    // (GIdleAction_v2), walking raises it by 10 up to 176 (GMoveAction_v2).
    if (state_ != agt::Idle && state_ != agt::Move) return;
    const auto anims = layerAnims(state_);
    if (anims.empty() || anims[0]->directions.empty() || anims[0]->directions[0].frames.empty()) return;
    const uint64_t loopMs = anims[0]->directions[0].frames.size() * kFrameMs;
    const uint64_t loops = (nowMs - std::min(nowMs, stateStart_)) / loopMs;
    for (; loops_ < loops; ++loops_) {
        if (state_ == agt::Idle) {
            if (visibility_ > baseVisibility_) visibility_ = uint8_t(std::max<int>(baseVisibility_, visibility_ - 10));
        } else if (visibility_ < 176) {
            visibility_ = uint8_t(std::min(176, visibility_ + 10));
        }
    }
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
    if (dead_) return std::nullopt;
    // GAgentPlug_v6 -> FUN_00521e00: the ranged weapon's energy comes back
    // at its recharge rate (plus its type 9 mods and a bonus from the
    // caller, not rebuilt), up to its maximum.
    if (ranged_) energy_ = std::min<double>(ranged_->ranged->maxEnergy, energy_ + dt * 0.001 * ranged_->ranged->recharge);
    // A new order (walking, steering) cuts the swing or the shot short.
    const bool busy = state_ == agt::Melee || state_ == agt::Shoot;
    if (busy && target_ < 0 && (steerX_ != 0 || steerY_ != 0 || !path_.empty())) setState(agt::Idle, nowMs);
    if (state_ == agt::Melee) return stepMelee(nowMs);
    if (state_ == agt::Shoot) {
        stepShoot(nowMs);
        return std::nullopt;
    }
    if (target_ >= 0 && rangedMode_) {
        if (!targetAlive_) {
            target_ = -1;
            path_.clear();
        } else if (canShoot()) {
            path_.clear();
            if (energy_ >= ranged_->ranged->cost) {
                faceTarget();
                setState(agt::Shoot, nowMs);
                swing_.start(nowMs);
                stepShoot(nowMs);
                return std::nullopt;
            }
        } else if (path_.empty()) {
            target_ = -1;  // nowhere left to walk: give up
        }
    } else if (target_ >= 0) {
        if (!targetAlive_) {
            target_ = -1;
            path_.clear();
        } else if (std::hypot(targetX_ - x_, targetY_ - y_) <= kReach) {
            path_.clear();
            faceTarget();
            setState(agt::Melee, nowMs);
            swing_.start(nowMs);
            return stepMelee(nowMs);
        } else if (path_.empty()) {
            target_ = -1;  // nowhere left to walk: give up
        }
    }
    if (steerX_ != 0 || steerY_ != 0) stepSteer(step);
    else if (!path_.empty()) followPath(path_, x_, y_, facing_, step);
    setState((steerX_ != 0 || steerY_ != 0 || !path_.empty()) ? agt::Move : agt::Idle, nowMs);
    noticeLoops(nowMs);
    return std::nullopt;
}

std::optional<Player::Blow> Player::stepMelee(uint64_t nowMs) {
    const auto anims = layerAnims(agt::Melee);
    // At the -3 tag: another swing while the target can still be hit.
    const auto r = swing_.advance(anims, facing_, nowMs, kFrameMs, [&] {
        if (target_ < 0 || !targetAlive_ || std::hypot(targetX_ - x_, targetY_ - y_) > kReach + 0.25) return false;
        faceTarget();
        return true;
    });
    std::optional<Blow> blow;
    if (r.blow && target_ >= 0) blow = Blow{target_, x_ + r.blow->x, y_ + r.blow->y};
    if (r.ended) {
        setState(agt::Idle, nowMs);  // the swing is over
        if (!targetAlive_) target_ = -1;
    }
    return blow;
}

void Player::stepShoot(uint64_t nowMs) {
    const auto anims = layerAnims(agt::Shoot);
    const double cost = ranged_ ? ranged_->ranged->cost : 0;
    // At the -3 tag: another shot while the target lives, can still be shot
    // at and the energy lasts (a rebuild condition: the original's
    // GAttackAction decides, not traced).
    const auto r = swing_.advance(anims, facing_, nowMs, kFrameMs, [&] {
        if (target_ < 0 || !targetAlive_ || energy_ < cost || !canShoot()) return false;
        faceTarget();
        return true;
    });
    if (r.aim) aim(*r.aim);
    if (r.blow && ranged_) {
        // GAgentPlug_v14 with the weapon in ranged mode: the muzzle is the
        // weapon layer's tag point; an aim point nearer than 2 cells is
        // pushed out to 2 cells along the facing; the bolt heads from the
        // muzzle to the aim point (GAgentPlug_v19 -> FUN_00520bc0).
        const double mx = x_ + r.blow->x, my = y_ + r.blow->y;
        double ax = aimX_, ay = aimY_;
        if ((ax - x_) * (ax - x_) + (ay - y_) * (ay - y_) < 4.0) {
            const uint8_t f = facingTowards(ax - x_, ay - y_);
            ax = x_ + facingCos(f) * 2.0, ay = y_ + facingSin(f) * 2.0;
        }
        shot_ = Shot{mx, my, r.height, facingTowards(ax - mx, ay - my)};
        energy_ = std::max(0.0, energy_ - cost);
        // GAgentPlug_v19: firing makes the shooter more noticeable, +30.
        visibility_ = uint8_t(std::min(255, visibility_ + 30));
    }
    if (r.ended) {
        setState(agt::Idle, nowMs);
        if (!targetAlive_) target_ = -1;
    }
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
    const uint64_t start = (state_ == agt::Melee || state_ == agt::Shoot) ? swing_.startMs() : stateStart_;
    const size_t step = size_t((nowMs - std::min(nowMs, start)) / kFrameMs);
    // The death animation plays once and keeps its last frame.
    const size_t frame = state_ == agt::Die ? std::min(step, frames - 1) : step % frames;
    pose.key = "player#" + std::to_string(int(state_)) + "#" + std::to_string(dir) + "#" + std::to_string(frame);
    pose.image = sprites_.compose(pose.key, anims, dir, frame);
    if (!pose.image) return pose;
    pose.anchorX = base.hotspot ? base.hotspot->first : pose.image->width / 2;
    pose.anchorY = base.hotspot ? base.hotspot->second : pose.image->height;
    return pose;
}

}  // namespace harbinger::world
