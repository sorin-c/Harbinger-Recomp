// The player's character on the map (GPlayer, agent class 0): where it
// stands, which way it faces, walking a path or following a stick, its
// blows, and its sprite built from layers (body, armour, weapon).
// Notes and evidence: docs/movement.md, docs/combat.md
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/agt.hpp"
#include "formats/anm.hpp"
#include "formats/animgroup.hpp"
#include "formats/mgr.hpp"
#include "formats/wpn.hpp"
#include "render/sprite.hpp"
#include "world/actor.hpp"
#include "world/combat.hpp"
#include "world/sprite_cache.hpp"
#include "world/walkmap.hpp"

namespace harbinger::world {

enum class Character { Human, Gladiator, Culibine };
const char* characterSection(Character c);  // the Harbinger.ini section: "Human", ...

class Player {
public:
    // Loads the character's template ([Human] Agent = dude.agt, ...) and its
    // animation groups, and stands it on `start` facing the entry point's
    // facing (FUN_00498250 puts the player on the level's entry point and
    // copies the entry's facing into plug+0x3a and +0x3c).
    Player(Vfs& vfs, const Ini& ini, Character character, const WalkMap& walk, const chk::EntryPoint& start);

    double x() const { return x_; }
    double y() const { return y_; }
    uint8_t facing() const { return facing_; }
    agt::State state() const { return state_; }
    const agt::Template& agentTemplate() const { return template_; }
    // Map cells per millisecond (the template's move animation, see walkSpeed).
    double speed() const { return speed_; }
    // The clearance limit for the template's size (stat 6, GAgentPlug_v31).
    uint8_t limit() const { return limit_; }

    // Click to move: finds a path to (x, y) (GMoveAction, FUN_004239f0) and
    // walks it. Returns false if there is nowhere to go.
    bool walkTo(double x, double y);
    // A scripted move (MoveTo on the player, as a portal's walk in and out):
    // like walkTo, but when the path stops short of (x, y) the player walks
    // the rest in a straight line. A rebuild choice: at the player's
    // clearance limit some portal doorways end a cell before the portal's
    // inner trigger, and the original's scripted walk reaches it.
    bool walkStraightOn(double x, double y);
    // Direct control (a rebuild addition for sticks and keys): walk along map
    // direction (dx, dy) while it is non-zero, sliding along walls. Cancels
    // a click path. (0, 0) stops.
    void steer(double dx, double dy);
    // The waypoints still ahead on a click path.
    const std::vector<WalkMap::Point>& path() const { return path_; }

    // Attack the agent `id` standing at (x, y): walk up to it, then swing
    // the melee animation (state 4) at it, again and again while it lives
    // (the -3 tag loops the swing). Walking or steering stops it.
    void attack(int id, double x, double y);
    // Shoot the agent `id` standing at (x, y) with the ranged weapon: walk
    // until it is within kShootRange and the line of fire is clear, then
    // play the shoot animation (state 3) at it: the -2 tag aims, the
    // weapon layer's tag fires (GAgentPlug_v14 -> v19), the -3 tag goes
    // round again while the target lives and the energy lasts. Without a
    // ranged weapon it is attack().
    void shoot(int id, double x, double y);
    // Where the target is now, whether it can still be hit, and how it is
    // moving (map cells per ms, for leading it; and where its walk ends);
    // call before update while attacking.
    void trackTarget(double x, double y, bool alive, double vx = 0, double vy = 0,
                     std::optional<WalkMap::Point> dest = {});
    // The agent being attacked, or -1.
    int target() const { return target_; }
    // Shooting rather than hitting it.
    bool shooting() const { return target_ >= 0 && rangedMode_; }
    // A rebuild choice: how near the player goes to shoot, in map cells
    // (the enemies' range comes from their shoot animation; the player's
    // is not traced).
    static constexpr double kShootRange = 8.0;
    // The height the line of fire is checked at before shooting (a rebuild
    // stand-in for the muzzle's, about 0.6 cells for the Human's rifle).
    static constexpr double kMuzzleGuess = 0.6;
    // Map cells from the target at which the swing starts (a rebuild choice:
    // the blow lands about 0.65 cells ahead, the target's box is ~0.5 wide).
    static constexpr double kReach = 0.9;
    // Animation frame time (agent+0x1fa, 100 unless an attack speed sets it).
    static constexpr uint64_t kFrameMs = 100;

    // A blow landing: the segment from the player to (x, y) is what must
    // cross a target's box (FUN_004a2590).
    struct Blow {
        int target = -1;
        double x = 0, y = 0;
    };
    // Advances to `nowMs`; returns the blow if one lands in this step.
    std::optional<Blow> update(uint64_t nowMs);
    // A shot fired in the last update: where the bolt leaves the muzzle, at
    // which height, and its heading.
    struct Shot {
        double x = 0, y = 0, z = 0;
        uint8_t heading = 0;
    };
    std::optional<Shot> takeShot() { return std::exchange(shot_, std::nullopt); }

    // The weapon the blows use (the equipped weapon's .wpn), and the
    // attack rating (GHumanPlayerPlug's +0x23c, 15 for a new character).
    const wpn::Weapon& weapon() const { return weapon_; }
    int meleeRating() const { return 15; }
    // The ranged weapon (the first equipped weapon of kind 1-3: the
    // Human's rifle), its rating (+0x23e, 15) and its energy (weapon+0x58,
    // up to +0x60; a shot costs +0x68, and it comes back at +0x70 a second,
    // FUN_00521e00 from GAgentPlug_v6).
    const wpn::Weapon* rangedWeapon() const { return ranged_ ? &*ranged_ : nullptr; }
    int rangedRating() const { return 15; }
    double energy() const { return energy_; }
    double maxEnergy() const { return ranged_ ? ranged_->ranged->maxEnergy : 0; }
    // The speed the ranged weapon's bolts fly at (map cells per ms), for
    // leading a moving target; set by whoever knows the effects.
    void setBoltSpeed(double cellsPerMs) { boltSpeed_ = cellsPerMs; }

    // Hit points (plug+0x88) and their maximum (+0x90): each character's
    // constructor sets its own (Human 70, FUN_004f26c0; Gladiator 80,
    // FUN_004baa80; Culibine 60, FUN_0046fe50).
    double hitPoints() const { return hitPoints_; }
    double maxHitPoints() const { return maxHitPoints_; }
    bool dead() const { return dead_; }
    uint64_t diedAt() const { return diedAt_; }
    // Takes a hit (GAgentPlug_v61): at 0 hit points the player dies, plays
    // its death animation once (GDieAction) and lies still.
    void takeDamage(int damage, uint64_t nowMs);
    // Hit points back (a health pack; the amount is the caller's), up to the
    // maximum; nothing for the dead.
    void heal(double points) {
        if (!dead_ && points > 0) hitPoints_ = std::min(maxHitPoints_, hitPoints_ + points);
    }
    // Hit points and ranged energy brought from the last level (a portal).
    void carry(double hitPoints, double energy) {
        hitPoints_ = std::min(maxHitPoints_, hitPoints);
        energy_ = std::min(maxEnergy(), energy);
    }
    // How the player stands against an attack roll: the template's defence
    // and resistances, with the armour's class added to the defence
    // (FUN_004f0f40, FUN_004f0fb0) and its levels per damage type.
    const Defender& defender() const { return defender_; }
    // The box half-size blows are tested against (template+0x20, slot 0x90).
    double radius() const { return template_.d20; }
    // How easily enemies notice the player (plug+0x38): an agent sees it
    // only above its threshold (FUN_00424700). It rises while the player
    // walks and fights and settles back while it stands (see docs/enemies.md);
    // 0 once dead.
    uint8_t visibility() const { return visibility_; }

    // The sprite to draw at `nowMs`: the layers of the current state's
    // animation in the facing's direction, composited, and the point in it
    // that sits on the player's map position. `key` names it for caches.
    struct Pose {
        std::string key;
        const render::RgbaImage* image = nullptr;
        int anchorX = 0, anchorY = 0;
    };
    Pose pose(uint64_t nowMs);

    // The animation groups the sprite is built from, bottom first.
    const std::vector<std::string>& layers() const { return layers_; }

private:
    void setState(agt::State s, uint64_t nowMs);
    void stepSteer(double step);
    std::optional<Blow> stepMelee(uint64_t nowMs);
    void stepShoot(uint64_t nowMs);
    bool canShoot() const;
    void aim(int delayMs);
    void faceTarget();
    void noticeLoops(uint64_t nowMs);
    std::vector<const anm::Animation*> layerAnims(agt::State s);

    const WalkMap& walk_;
    SpriteCache sprites_;
    wpn::Weapon weapon_;
    agt::Template template_;
    mgr::Index animIndex_;
    animgroup::Index groups_;
    std::vector<std::string> layers_;
    double speed_ = 0;
    uint8_t limit_ = 2;

    double x_ = 0, y_ = 0;
    uint8_t facing_ = 0;
    agt::State state_ = agt::Idle;
    uint64_t stateStart_ = 0, lastMs_ = 0;
    double halfLastDt_ = 0;  // GAgentPlug_v80 averages this frame's time step with the last one's
    std::vector<WalkMap::Point> path_;
    double steerX_ = 0, steerY_ = 0;
    bool started_ = false;

    int target_ = -1;
    double targetX_ = 0, targetY_ = 0;
    bool targetAlive_ = false;
    double targetVx_ = 0, targetVy_ = 0;
    std::optional<WalkMap::Point> targetDest_;
    bool rangedMode_ = false;
    Swing swing_;

    std::optional<wpn::Weapon> ranged_;
    double energy_ = 0;
    double boltSpeed_ = 0;
    double aimX_ = 0, aimY_ = 0;  // plug+0x78, +0x80: where the shot goes
    std::optional<Shot> shot_;

    double hitPoints_ = 0, maxHitPoints_ = 0;
    bool dead_ = false;
    uint64_t diedAt_ = 0;
    Defender defender_;
    uint8_t visibility_ = 128, baseVisibility_ = 128;  // plug+0x38, +0x39
    uint64_t loops_ = 0;  // idle or walk animation loops completed in this state
};

}  // namespace harbinger::world
