// Converts decoded .gim frames to 32-bit pixels for upload to the GPU.
#pragma once

#include <cstdint>
#include <vector>

#include "formats/gim.hpp"

namespace harbinger::render {

// RLE sprites reserve palette entries 0 and 1 for two marker colours: pure
// green (painted where a shadow falls) and pure red (a one-pixel rim around
// the figure). Their meaning is still a guess (see gim.md): green as the
// shadow and red as the hover-highlight outline.
enum class KeyMode {
    Shadow,       // green as translucent black, red hidden (current best guess)
    Highlight,    // as Shadow, but draw the red rim (object under the cursor)
    Transparent,  // drop both
    Raw,          // draw the palette colours as stored (for inspection)
};

// RGBA8888 pixels, R in the lowest byte (SDL_PIXELFORMAT_ABGR8888 on little endian).
std::vector<uint32_t> toRgba(const gim::Image& img, size_t frame, KeyMode mode = KeyMode::Shadow);

}  // namespace harbinger::render
