// Levels (.chk "chunks"): header, entry points, the map cells that paint the
// floor and walls, and the set gizmos (level objects). Agents, triggers and
// the rest of the file are not decoded yet.
// Format notes and evidence: docs/formats/level.md
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <string>
#include <vector>

namespace harbinger::giz {
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
    size_t gizmosEnd = 0;     // file offset where the undecoded part starts

    const Cell& at(int x, int y) const { return cells[size_t(x) * height + size_t(y)]; }
    // Whether variant `v` keeps a gizmo instance.
    bool keeps(const GizmoRecord& g, size_t v = 0) const;
};

// Throws std::runtime_error on malformed data. Stops after the map cells.
Level parse(const std::vector<uint8_t>& data);

// Parses the instance lists and set gizmos that follow the cells. A record's
// length depends on its template's class, so `lookup` maps a gizmo.mgr id to
// its template (nullptr if unknown, which fails the parse).
void parseGizmos(Level& level, const std::vector<uint8_t>& data,
                 const std::function<const giz::Template*(uint16_t)>& lookup);

}  // namespace harbinger::chk
