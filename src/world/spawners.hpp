// Spawners: gizmos that hold agents until a trigger or script activates
// them, then let them out at a set frame of their animation. Notes and
// evidence: docs/formats/gizmo.md, "Spawners".
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "engine/vfs.hpp"
#include "formats/chk.hpp"
#include "world/level.hpp"
#include "world/sprite_cache.hpp"

namespace harbinger::world {

// The agent a spawner slot holds is placed in the level like any other; its
// record's trailing word (GEnemyPlug_v71, plug+0x218) names the spawner's
// instance in its low 12 bits and the slot in the top 4 (FUN_0044b6b0).
struct SpawnBinding {
    uint16_t spawner = 0;
    uint8_t slot = 0;
};
std::optional<SpawnBinding> spawnBinding(const chk::AgentRecord& a);

// Where a spawner's slot puts its agent: the template's spawn point (a
// pixel of the first frame, GSpawnGizmo_v4) turned into a map position the
// way FUN_004b3690 does for use points.
Level::MapPoint spawnPoint(Level& level, const chk::GizmoRecord& g, size_t slot);

// What an agent's class does when its spawner lets it out (the class's
// GIActivatable slot 1).
enum class Wake : uint8_t {
    None,   // NPCs and Gubiests: nothing (the slot does nothing; such agents are never held)
    Awake,  // Vantir, Khapra, Bowel Horse, Herps, bosses: an awake policy, which plays the Awake animation
    Ready,  // drones and turrets: straight to the Idle action; Boxers: their mind's slot 12
};
Wake wakeFor(uint16_t agentClass);

class Spawners {
public:
    Spawners(Vfs& vfs, Level& level);

    // An agent its spawner has not let out yet (it waits in the Stasis
    // action, see the notes). Agents of spawners the level starts spent,
    // and agents whose class has nothing to wake (Wake::None), are free.
    bool held(const chk::AgentRecord& a) const;

    // Once a frame, after the level's gizmos have updated: spawners and
    // splitters that reached their BeginActivate frame activate the gizmos
    // attached to them (FUN_004bdaf0 from GSpawnGizmoPlug_v28's slot 30);
    // spawners that reached their EndActivate frame (the last if none) let
    // out their agents. Returns the agents let out.
    std::vector<const chk::AgentRecord*> update(uint64_t nowMs);

    // The picture of an agent the spawners are showing: a held agent stands
    // on the first frame of its Awake animation (state 7; nothing if it has
    // none), and one let out with Wake::Awake plays that animation through
    // once. Empty when the agent is shown as usual.
    std::optional<Level::Pose> pose(const chk::AgentRecord& a, uint64_t nowMs);

    static constexpr int16_t kAwake = 7;  // the agent state the Stasis and Awake actions play

private:
    struct Watch {
        const chk::GizmoRecord* gizmo = nullptr;
        size_t attachFrame = 0;   // BeginActivate (gizmo+0xb8, GSpawnGizmo_v34)
        size_t releaseFrame = 0;  // EndActivate or the last frame (gizmo+0xc0, GSpawnGizmo_v4)
        bool attached = false, released = false;
    };
    struct Freed {
        uint64_t atMs = 0;
    };

    Level& level_;
    SpriteCache sprites_;
    std::vector<Watch> watches_;
    std::map<uint16_t, std::vector<const chk::AgentRecord*>> slots_;  // spawner -> agents it holds
    std::map<uint16_t, bool> held_;                                    // agent instance -> held
    std::map<uint16_t, Freed> awake_;                                  // agent instance -> playing Awake
};

}  // namespace harbinger::world
