// The level's enemies: their hit points, the blows that land on them and
// their death, and their minds: noticing the player, walking up to it and
// hitting it, wandering, fleeing.
// Notes and evidence: docs/combat.md, docs/enemies.md
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/chk.hpp"
#include "formats/wpn.hpp"
#include "world/actor.hpp"
#include "world/combat.hpp"
#include "world/level.hpp"
#include "world/sprite_cache.hpp"
#include "world/walkmap.hpp"

namespace harbinger::world {

// The player as the enemies see it.
struct Quarry {
    double x = 0, y = 0;
    double radius = 0;       // template+0x20 (slot 0x90)
    uint8_t visibility = 0;  // plug+0x38
    bool alive = false;
    const Defender* defender = nullptr;
};

class Foes {
public:
    // Every agent the level keeps whose class is an enemy (1-19, 37-40: not
    // the player, the NPCs 20-28, the mines 29-32 and 41, or the cameras
    // 33-36), with hit points rolled from its template (FUN_00408110), its
    // melee weapon (stat 4) and a fresh mind.
    Foes(Vfs& vfs, const Ini& ini, Level& level, const WalkMap& walk, Rng& rng);

    // What an enemy is doing (its mind's current action, GIAIPolicy steps).
    enum class Action { Idle, Move, Attack };
    // The plan the actions come from (GIAIPolicy ids in brackets).
    enum class Policy { None, Move /*0*/, Attack /*4*/, Flee /*0xd*/, Wander /*0xe*/ };

    // GRoleMind (vtable 0x5462f0 area) and the thinker's state.
    struct Mind {
        uint16_t thinker = 0;      // template+0x58
        uint8_t emotion[3] = {};   // mind+0x54..+0x56 (GInstinctCtr): 0 courage, 1 restlessness, 2 the
                                   // visibility an agent must exceed to be seen
        int role = 0;              // mind+0x5c[+0x68]: 0 guarding, 2 not guarding, 15 meditating
        int combat = 0;            // mind+0x74: 1 just engaged, 3 fleeing
        bool engaged = false;      // mind+0x78 holds the target last fought
        bool provoked = false;     // hit by the player (GRoleMind_v8): fights back at the next think
        Policy policy = Policy::None;
        int step = 0;              // the policy's action index (mind+0x3c)
        Action action = Action::Idle;
        double goalX = 0, goalY = 0;  // mind+0x10..+0x1c: where a move goes
        uint64_t lastThink = 0;    // mind+0x40
    };

    struct Foe {
        const chk::AgentRecord* record = nullptr;
        int hitPoints = 0, maxHitPoints = 0;
        bool dead = false;
        uint64_t diedAt = 0;  // when the death animation started

        double x = 0, y = 0;
        uint8_t facing = 0;
        agt::State state = agt::Idle;
        uint64_t stateStart = 0, lastMs = 0;
        size_t variant = 0;   // which of the state's animations plays (FUN_00407950)
        uint64_t loops = 0;   // loops of the idle animation completed
        std::vector<WalkMap::Point> path;
        double speed = 0;     // cells per ms (the move animation's parameter)
        uint8_t limit = 2;    // the walk grid clearance limit for its size (stat 6)
        int rating = 0;       // its attack rating: stat 0 * 5 (GAgentPlug_v100)
        double reach = 0;     // plug+0x230: melee reach beyond the target's radius
        wpn::Weapon weapon;   // the melee weapon (stat 4)
        bool armed = false;
        Swing swing;
        Mind mind;
    };
    const std::vector<Foe>& all() const { return foes_; }
    const Foe* find(int id) const;
    // The id of a live foe whose box (template+0x20 + 0.1 either side,
    // FUN_004a2590) holds map point (x, y), the nearest if several; or -1.
    int at(double x, double y) const;
    // The nearest live foe within `range` cells of (x, y), preferring those
    // ahead along `facing` (a rebuild addition for pads); or -1.
    int nearest(double x, double y, double range, std::optional<uint8_t> facing = {}) const;

    // A blow from (fromX, fromY) to (toX, toY) by an attacker with `rating`
    // and `weapon` (melee damage only): lands on foe `id` if the segment
    // crosses its box, then rolls the hit (FUN_00452290). A foe at 0 hit
    // points or below starts dying at `nowMs`; one that lives turns on the
    // attacker if it had not seen it. Returns the strike if the segment
    // reached the foe.
    std::optional<Strike> blow(int id, double fromX, double fromY, double toX, double toY, int rating,
                               const wpn::Weapon& weapon, uint64_t nowMs);

    // A hit rolled elsewhere (a bolt) lands on foe `id`: its hit points go
    // down, at 0 or below it starts dying at `nowMs`, otherwise it turns on
    // the attacker (GRoleMind_v8).
    void hurt(int id, const Strike& s, uint64_t nowMs);
    // The defence a bolt rolls against.
    Defender defender(int id) const { return defenderOf(tmpl(foes_[size_t(id)])); }

    // A blow by foe `foe` that reached the player.
    struct Hit {
        int foe = -1;
        Strike strike;
    };
    // Runs every live foe's mind and body to `nowMs` against the player;
    // returns the blows that reached it.
    std::vector<Hit> update(uint64_t nowMs, const Quarry& player);

    // The foe's picture: what it is doing, in the direction it faces; dead
    // foes play their death animation (state 5) once and lie on its last
    // frame. Agents that are not foes are the level's idle picture.
    Level::Pose pose(const chk::AgentRecord& a, uint64_t nowMs);
    // Where an agent stands now (a foe moves; other agents stay put).
    WalkMap::Point position(const chk::AgentRecord& a) const;

    static bool isEnemyClass(uint16_t cls) { return (cls >= 1 && cls <= 19) || (cls >= 37 && cls <= 40); }
    // How far an agent sees, in cells (the thinkers' FUN_00424700 calls:
    // 9, or 5 while meditating), and how often its mind thinks (GMind_v6:
    // more than 249 ms since the last time).
    static constexpr double kSight = 9.0, kSightMeditating = 5.0;
    static constexpr uint64_t kThinkMs = 250;

private:
    const agt::Template& tmpl(const Foe& f) const { return level_.agentTemplate(*f.record); }
    std::vector<const anm::Animation*> anims(const Foe& f, agt::State s) const;
    void setState(Foe& f, agt::State s, uint64_t nowMs);
    bool sees(const Foe& f, const Quarry& q) const;
    bool inReach(const Foe& f, const Quarry& q) const;
    // The thinker: true if it started a new plan.
    bool decide(Foe& f, const Quarry& q, uint64_t nowMs);
    bool engage(Foe& f, const Quarry& q, bool mayFlee, uint64_t nowMs);
    void rest(Foe& f, const Quarry& q, uint64_t nowMs);
    bool startPolicy(Foe& f, Policy p, const Quarry& q, uint64_t nowMs, double gx = 0, double gy = 0);
    void prepareStep(Foe& f, const Quarry& q, uint64_t nowMs);
    void nextStep(Foe& f, const Quarry& q, uint64_t nowMs);
    void tickPolicy(Foe& f, const Quarry& q, uint64_t nowMs);
    void moveTo(Foe& f, double x, double y, uint64_t nowMs);
    void startSwing(Foe& f, const Quarry& q, uint64_t nowMs);
    void think(Foe& f, const Quarry& q, uint64_t nowMs);
    std::optional<Hit> act(int id, Foe& f, const Quarry& q, uint64_t nowMs);
    std::optional<uint8_t> pickHeading(const Foe& f, uint8_t heading, double dist);
    bool fleePoint(Foe& f, const Quarry& q);

    Level& level_;
    const WalkMap& walk_;
    Rng& rng_;
    mutable SpriteCache sprites_;
    std::vector<Foe> foes_;
};

}  // namespace harbinger::world
