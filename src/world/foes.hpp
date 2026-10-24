// The level's enemies as things that can be hit: their hit points, the
// blows that land on them, and their death.
// Notes and evidence: docs/combat.md
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "engine/vfs.hpp"
#include "formats/chk.hpp"
#include "world/combat.hpp"
#include "world/level.hpp"
#include "world/sprite_cache.hpp"

namespace harbinger::world {

class Foes {
public:
    // Every agent the level keeps whose class is an enemy (1-19, 37-40: not
    // the player, the NPCs 20-28, the mines 29-32 and 41, or the cameras
    // 33-36), with hit points rolled from its template (FUN_00408110).
    Foes(Vfs& vfs, Level& level, Rng& rng);

    struct Foe {
        const chk::AgentRecord* record = nullptr;
        int hitPoints = 0, maxHitPoints = 0;
        bool dead = false;
        uint64_t diedAt = 0;  // when the death animation started
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
    // points or below starts dying at `nowMs`. Returns the strike if the
    // segment reached the foe.
    std::optional<Strike> blow(int id, double fromX, double fromY, double toX, double toY, int rating,
                               const wpn::Weapon& weapon, uint64_t nowMs);

    // The foe's picture: dead foes play their death animation (state 5) once
    // and lie on its last frame; live ones are the level's idle picture.
    Level::Pose pose(const chk::AgentRecord& a, uint64_t nowMs);

    static bool isEnemyClass(uint16_t cls) { return (cls >= 1 && cls <= 19) || (cls >= 37 && cls <= 40); }

private:
    Level& level_;
    Rng& rng_;
    SpriteCache sprites_;
    std::vector<Foe> foes_;
};

}  // namespace harbinger::world
