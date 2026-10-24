// Animated cursors (.acr): a hotspot and a list of frame images.
// Format notes and evidence: docs/formats/acr.md
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace harbinger::acr {

struct Cursor {
    uint32_t frameDelay = 0;  // 10 in every file; unit not confirmed
    int16_t hotX = 0, hotY = 0;  // the engine draws the frame at (mouse - hotspot)
    std::vector<std::string> frames;  // image paths (".tga", served from .gim)
};

// Throws std::runtime_error on malformed data or trailing bytes.
Cursor parse(const std::vector<uint8_t>& data);

}  // namespace harbinger::acr
