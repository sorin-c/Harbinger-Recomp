// The player's character on the map (GPlayer, agent class 0): where it
// stands, which way it faces, walking a path or following a stick, its
// blows, and its sprite built from layers (body, armour, weapon).
// Notes and evidence: docs/movement.md, docs/combat.md
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/agt.hpp"
#include "formats/anm.hpp"
#include "formats/animgroup.hpp"
#include "formats/mgr.hpp"
#include "formats/wpn.hpp"
#include "render/sprite.hpp"
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
    // Where the target is now, and whether it can still be hit; call before
    // update while attacking.
    void trackTarget(double x, double y, bool alive);
    // The agent being attacked, or -1.
    int target() const { return target_; }
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

    // The weapon the blows use (the equipped weapon's .wpn), and the
    // attack rating (GHumanPlayerPlug's +0x23c, 15 for a new character).
    const wpn::Weapon& weapon() const { return weapon_; }
    int meleeRating() const { return 15; }

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
    void stepPath(double step);
    void stepSteer(double step);
    std::optional<Blow> stepMelee(uint64_t nowMs);
    void faceTarget();
    std::vector<const anm::Animation*> layerAnims(agt::State s);
    // The map offset of the blow tag at `frame` in direction `dir`, if any.
    std::optional<WalkMap::Point> blowOffset(size_t dir, uint16_t frame);

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
    int64_t meleeFrame_ = -1;  // the last melee frame whose tags have run
};

}  // namespace harbinger::world
