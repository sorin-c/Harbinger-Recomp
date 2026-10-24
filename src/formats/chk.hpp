// Levels (.chk "chunks"): header, entry points, the map cells that paint the
// floor and walls, and the set gizmos (level objects). Agents, triggers and
// the rest of the file are not decoded yet.
// Format notes and evidence: docs/formats/level.md
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <string>
#include <vector>

namespace harbinger::giz {
struct Template;
}
namespace harbinger::agt {
struct Template;
}

namespace harbinger::chk {

// A place the player can arrive at (GEntryPt).
struct EntryPoint {
    uint16_t id = 0;
    double x = 0, y = 0;  // map cells
    uint16_t facing = 0;
    std::string name;
    int32_t link = 0;  // -1 for "Default"; meaning not confirmed
};

// One brush tile on a cell (FUN_004e2d60).
struct Layer {
    uint16_t brush = 0;  // id in the act's brush.mgr
    uint16_t tile = 0;   // brs tile id: (group << 12) | index
};

struct Cell {
    Layer layers[2];
    uint8_t count = 0;  // 0..2; layer 0 is the floor, layer 1 a wall or edge on top
};

// A gizmo placed on the map (FUN_004495a0): a gizmo.mgr template id, then the
// plug's chunk load, whose first six bytes every class shares (0x4b9440).
struct GizmoRecord {
    uint16_t gizmo = 0;     // id in the act's gizmo.mgr
    uint16_t instance = 0;  // unique within the level
    uint16_t x = 0, y = 0;  // map cell
    std::vector<uint8_t> extra;  // the class's own fields (giz::Template::recordExtra)
};

// An alternative version of the level (FUN_00448080): cells it replaces. The
// chunk load takes a variant number; 0 is the base level. No shipped level
// has variants beyond the base.
struct Variant {
    std::string name;
    std::vector<std::pair<uint16_t, Cell>> cells;  // (index into Level::cells, replacement)
};

// A placed sound (GSoundInfo, FUN_005037c0).
struct SoundInfo {
    double x = 0, y = 0;
    uint16_t sound = 0;   // sound.mgr id (likely)
    uint16_t unknown = 0;
    uint32_t unknown2 = 0;
};

// An agent placed on the map (FUN_00418c40): an agent.mgr template id, then
// the plug's chunk load (GAgentPlug 0x420520, NPCs and enemies 0x4e6670).
struct AgentRecord {
    uint16_t agent = 0;     // id in agent.mgr
    uint16_t instance = 0;
    uint8_t facing = 0;     // plug+0x3a; 256 steps a turn, see anm::directionFor
    uint8_t unknown = 0;
    std::vector<uint16_t> slots;  // slot 18 (0x4e6590)
    double x = 0, y = 0;    // map position, in cells
    std::string label;      // slot 7: the name shown for the agent, often empty
    uint8_t flag = 0;       // plug+0x1f3
    uint16_t words[2] = {}; // plug+0x1f6, +0x1f8
    uint16_t extra = 0;     // 0x4e6670's word (not cameras or players)
};

// A trigger area (GGizmoTrigger_v6, 0x51a440): a circle on the map.
struct Trigger {
    double x = 0, y = 0, radius = 0;  // map cells
    uint16_t unknown = 0;
    uint16_t id = 0;                  // trigger+0x28
};

// An event trigger (GEventTrigger, FUN_0048d200): when the player enters,
// its event (GEvent, FUN_0048c980) runs.
struct EventTrigger : Trigger {
    double radius2 = 0;               // trigger+0x48; equals radius in the files seen
    // FUN_0048ca20: gizmo instances with 8 bytes each (undecoded).
    std::vector<std::pair<uint16_t, std::array<uint8_t, 8>>> gizmos;
    uint32_t word = 0;                // event+0x2c
    uint8_t flag = 0;                 // event+0x28
    uint16_t word2 = 0;               // event+0x34
    std::vector<std::pair<uint16_t, SoundInfo>> sounds;  // FUN_0048cd20
    std::vector<uint16_t> ids;        // FUN_0048cbc0: agent or gizmo instances
    uint16_t script = 0;              // version > 1.19: event+0x3c, a script id (0 = none)
};

// A gizmo trigger (FUN_004baa50): the area fires a gizmo.
struct GizmoTrigger : Trigger {
    uint16_t gizmo = 0;               // the gizmo's instance
};

// A beacon (GBeacon_v4 + GRoomBeacon_v4, 0x4fb010): a navigation point in a
// room, with the beacons it links to and a cost for each link.
struct Beacon {
    double x = 0, y = 0;
    uint16_t room = 0;
    std::vector<std::pair<uint16_t, uint32_t>> links;  // (beacon, cost)
};

// A room (FUN_004fad80): an id and two groups of four words, undecoded.
struct Room {
    uint16_t id = 0;
    uint16_t a[4] = {}, b[4] = {};
};

struct Level {
    double version = 0;  // 1.23 in every shipped level
    uint16_t act = 0;    // [ActN] section in Harbinger.ini: which brush and gizmo sets
    uint16_t width = 0, height = 0;  // in cells
    std::vector<EntryPoint> entries;
    uint16_t unknown1 = 0;
    uint16_t music = 0;  // sound.mgr id of the level's music, e.g. 302 = Haven.mp3
    uint16_t unknown3 = 0, unknown4 = 0;
    std::string name;    // shown to the player, e.g. "Torvus Junction"
    std::vector<Cell> cells;  // column-major: cells[x * height + y] (FUN_00447360)
    std::vector<Variant> variants;  // variants 1..n (the base level is variant 0)
    size_t cellsEnd = 0;      // file offset after the cells and variants

    // Filled by parseGizmos.
    // Per variant (0 = base), the sorted instance ids of the gizmos it keeps
    // (FUN_004477e0); FUN_004495a0 deletes every other gizmo it loads.
    std::vector<std::vector<uint16_t>> instanceLists;
    std::vector<GizmoRecord> gizmos;
    size_t gizmosEnd = 0;     // file offset after the gizmos

    // Filled by parseAgents: per variant the agent instances it keeps
    // (FUN_004479a0), then the agents.
    std::vector<std::vector<uint16_t>> agentLists;
    std::vector<AgentRecord> agents;
    size_t agentsEnd = 0;     // file offset after the agents

    // Filled by parseRest: the sections after the agents, to the end of the file.
    std::vector<EventTrigger> eventTriggers;   // FUN_0051a6a0
    std::vector<GizmoTrigger> gizmoTriggers;
    std::vector<SoundInfo> sounds;             // FUN_00504a90 (none in the shipped levels)
    // FUN_0044ab80: for gizmos that attach others (GIAttacher, FUN_004bdcc0)
    // the instances they carry (a console and the door it opens), and for
    // gizmos that pass power on (GIPowerable, 0x4d4ef0, which adds each
    // listed gizmo as a target) the instances they power.
    std::vector<std::pair<uint16_t, std::vector<uint16_t>>> attachments, powers;
    // GChunk_v3 (0x44b200): a word (chunk+6), then the gizmos that start with
    // power and how much.
    uint16_t powerWord = 0;
    std::vector<std::pair<uint16_t, uint16_t>> powerSources;
    std::vector<Beacon> beacons;               // FUN_00436460
    std::vector<Room> rooms;                   // FUN_004fb860

    const Cell& at(int x, int y) const { return cells[size_t(x) * height + size_t(y)]; }
    // Whether variant `v` keeps a gizmo or agent instance.
    bool keeps(const GizmoRecord& g, size_t v = 0) const;
    bool keeps(const AgentRecord& a, size_t v = 0) const;
};

// Throws std::runtime_error on malformed data. Stops after the map cells.
Level parse(const std::vector<uint8_t>& data);

// Parses the instance lists and set gizmos that follow the cells. A record's
// length depends on its template's class, so `lookup` maps a gizmo.mgr id to
// its template (nullptr if unknown, which fails the parse).
void parseGizmos(Level& level, const std::vector<uint8_t>& data,
                 const std::function<const giz::Template*(uint16_t)>& lookup);

// Parses the agent lists and agents after the gizmos (call parseGizmos
// first). `lookup` maps an agent.mgr id to its template, whose class decides
// the record's length.
void parseAgents(Level& level, const std::vector<uint8_t>& data,
                 const std::function<const agt::Template*(uint16_t)>& lookup);

// Parses everything after the agents (call parseAgents first): triggers,
// sounds, attachments, power, beacons and rooms. Fails unless the data ends
// exactly after the rooms.
void parseRest(Level& level, const std::vector<uint8_t>& data);

}  // namespace harbinger::chk
