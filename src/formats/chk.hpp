// Levels (.chk "chunks"): header, entry points and the map cells that paint
// the floor and walls. Gizmos, agents, triggers and the rest of the file are
// not decoded yet. Format notes and evidence: docs/formats/level.md
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
    size_t cellsEnd = 0;      // file offset where the undecoded part starts

    const Cell& at(int x, int y) const { return cells[size_t(x) * height + size_t(y)]; }
};

// Throws std::runtime_error on malformed data. Stops after the map cells.
Level parse(const std::vector<uint8_t>& data);

}  // namespace harbinger::chk
