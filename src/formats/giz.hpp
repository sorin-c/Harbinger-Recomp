// Gizmo templates (.giz): the level objects (machinery, trash, consoles,
// chests, spawners...) that a level's set-gizmo records place on the map.
// Format notes and evidence: docs/formats/gizmo.md
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace harbinger::giz {

// The class index in the low byte of the flags word picks the engine class
// (factory table behind FUN_004b3db0).
enum Class : uint8_t {
    Static = 0, Looped = 1, Chest = 2, Animated = 3, Bridge = 4, Console = 5, ChargingStn = 6,
    Umbilical = 7, UmbilicalShell = 8, Looped2 = 9, Incubator = 10, Pillar = 11, Breaker = 12,
    Splitter = 13, Barrel = 14, CompuConsole = 15, Portal = 16, RandomPause = 17, Spawn = 18,
    Container = 19, KeyConsole = 20, Flicker = 21, TwoWay = 22, TrashPile = 23, ClassCount = 24,
};

// Flag bits in the high byte of the flags word (FUN_004b7350).
enum Flags : uint16_t {
    HasScript = 0x8000,   // gizmo+0x10; the template ends with script blocks
    HasLight = 0x4000,    // gizmo+100; the template carries a light
    HasOffset = 0x2000,   // gizmo+0x34/+0x36 start at 0 and are read
};

// One cell of a mask's footprint, in half-cell units (64x32 diamonds)
// relative to the gizmo's origin half-cell.
struct MaskCell {
    int16_t dx = 0, dy = 0;
    int16_t kind = 0;   // 0x0d sorts into a separate list (FUN_004b5ca0); meaning not confirmed
    int16_t a = 0, b = 0;  // b bit 1 adds the cell to a third list
};

struct Mask {
    std::string path;     // a .tga path; the archives serve the .gim
    uint16_t rows = 0;    // the first mask's value is vtable slot 17 (0x431730): sets the draw anchor
    std::vector<MaskCell> cells;
};

struct Frame {
    uint16_t mask = 0;          // index into masks
    int16_t x = 0, y = 0;       // likely where the frame sits on the first frame's image, in pixels
    uint16_t durationMs = 0;    // 100 for most animation frames; likely a duration
    uint16_t code = 0;          // frame+0x1c: 0 on most frames, 1-10 on the rest; meaning not confirmed
    std::string path;
    bool hasRect = false;       // FUN_004ba840: two packed words, then four bytes
    uint8_t rect[8] = {};
};

struct SpawnPoint {
    int16_t a = 0, b = 0, c = 0;  // GSpawnGizmo_v4 entries; meaning not decoded
};

struct Template {
    double version = 0;   // 1.04 in shipped files
    std::string name;
    uint16_t flags = 0;
    uint8_t cls = 0;      // flags & 0xff
    uint16_t layout = 0;  // the word after the flags (FUN_004b1f10 splits it 8/4/4 bits)
    uint8_t unknownByte = 0;
    std::vector<Mask> masks;
    std::vector<Frame> frames;
    int16_t point[2] = {-1, -1};   // version > 1.03: gizmo+0x2e/+0x30
    int16_t offset[2] = {-1, -1};  // HasOffset: gizmo+0x34/+0x36
    std::string label;             // chests, barrels, consoles, containers: the name shown to the player
    std::vector<SpawnPoint> spawns;
    std::vector<uint8_t> classData;  // other class fields, undecoded
    std::vector<uint8_t> light;      // HasLight: undecoded
    std::vector<uint8_t> script;     // HasScript: undecoded blocks

    // Size of a set-gizmo record's class data in a level, after the shared
    // six bytes (see chk::parseGizmos).
    size_t recordExtra(const uint8_t* p, size_t avail) const;
};

// Throws std::runtime_error on malformed data or trailing bytes.
Template parse(const std::vector<uint8_t>& data);

}  // namespace harbinger::giz
