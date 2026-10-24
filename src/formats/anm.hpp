// Animations (.anm): per direction, the frame list and tags of one agent
// animation. The frames' images live in one .gim sheet per direction.
// Format notes and evidence: docs/formats/agent.md
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace harbinger::anm {

struct Direction {
    std::vector<std::string> frames;  // .tga names; see sheetPath
    std::vector<std::vector<uint8_t>> actions;  // GActionTag, 6 bytes each, undecoded
    std::vector<uint16_t> frameWords;           // one per frame when Animation::frameWords
};

struct Animation {
    uint16_t header = 0;
    bool frameWords = false;          // header bit 0x10 (anim+0x444)
    std::optional<std::pair<int16_t, int16_t>> hotspot;  // GHotSpotTag: likely the ground point in a frame
    std::vector<std::vector<uint8_t>> sounds;  // GSoundTag, 4 bytes each, undecoded
    std::vector<Direction> directions;         // 16, or the header's even count
    std::vector<std::string> extraFrames;      // read into slot 0x10 after the directions

    // The first direction with frames, or nullptr.
    const Direction* firstDirection() const;
};

// The direction index for a facing (0-255, one full turn) in an animation
// with `count` directions (FUN_004e12d0). Only 10 and 16 directions have a
// table; other counts give 0, as in the original.
size_t directionFor(uint8_t facing, size_t count);

// Throws std::runtime_error on malformed data or trailing bytes.
Animation parse(const std::vector<uint8_t>& data);

// The frame sheet that holds a direction's frames (FUN_00429f30): for
// "...\Idle\0\00.tga" it is "...\Idle\fsp\Idle0FSP.gim".
std::string sheetPath(const std::string& framePath);

}  // namespace harbinger::anm
