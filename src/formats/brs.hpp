// Brushes (.brs): the floor and wall tile sets a level's map cells paint with.
// Format notes and evidence: docs/formats/level.md
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace harbinger::brs {

// The 13 tile groups, in file order. A tile id is (group << 12) | index.
enum Group : uint8_t {
    Centre = 0,  // seamless 256x256 textures; one image serves 16 tile ids
    N, NE, E, SE, S, SW, W, NW,  // edges and corners
    FN, FE, FS, FW,              // the F* pieces (meaning not confirmed)
    GroupCount
};

struct Tile {
    uint16_t id = 0;
    std::string path;  // as stored, with a ".tga" extension the archives serve as ".gim"
};

struct Brush {
    std::string name;
    uint16_t kind = 0;    // 1 for wall sets, 0 for floors
    uint16_t height = 0;  // in 64-pixel units: walls rise height * 64 px above the floor
    uint8_t walkable = 0; // 1 = walkable floor (FUN_0044a230 also treats 2 specially)
    std::vector<Tile> groups[GroupCount];
    std::vector<std::pair<uint16_t, std::string>> categories;  // editor-only names
};

// Throws std::runtime_error on malformed data or trailing bytes.
Brush parse(const std::vector<uint8_t>& data);

// The tile and the image it draws with for `id`, as GBrush registers them
// (FUN_0043f290): a centre entry with id 0xN0 covers 0xN0..0xNF (low nibble =
// position inside the texture), other entries cover exactly their id.
// Returns nullptr when the brush has no such tile.
const Tile* find(const Brush& brush, uint16_t id);

// Where a centre tile's 128x64 diamond sits inside its 256x256 texture
// (FUN_004e2eb0): the top-left of the diamond's bounding box, wrapping.
struct Offset {
    int x, y;
};
inline Offset centreOffset(uint16_t id) {
    const int k = id & 0xF;
    return {128 * (k & 1) + 64 * ((k >> 1) & 1), 32 * (k >> 1)};
}

}  // namespace harbinger::brs
