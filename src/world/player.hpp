// The player's character on the map (GPlayer, agent class 0): where it
// stands, which way it faces, walking a path or following a stick, and its
// sprite built from layers (body, armour, weapon).
// Notes and evidence: docs/movement.md
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/agt.hpp"
#include "formats/anm.hpp"
#include "formats/animgroup.hpp"
#include "formats/gim.hpp"
#include "formats/mgr.hpp"
#include "render/sprite.hpp"
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
    // Direct control (a rebuild addition for sticks and keys): walk along map
    // direction (dx, dy) while it is non-zero, sliding along walls. Cancels
    // a click path. (0, 0) stops.
    void steer(double dx, double dy);
    // The waypoints still ahead on a click path.
    const std::vector<WalkMap::Point>& path() const { return path_; }

    void update(uint64_t nowMs);

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
    std::vector<const anm::Animation*> layerAnims(agt::State s);
    const gim::Image* sheet(const std::string& path);

    Vfs& vfs_;
    const WalkMap& walk_;
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

    std::map<std::string, std::unique_ptr<anm::Animation>> anims_;   // by .anm path
    std::map<std::string, std::unique_ptr<gim::Image>> sheets_;      // by sheet path
    std::map<std::tuple<int, size_t, size_t>, render::RgbaImage> poses_;  // by state, direction, frame
};

}  // namespace harbinger::world
