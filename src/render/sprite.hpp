// Converts decoded .gim frames to 32-bit pixels for upload to the GPU.
#pragma once

#include <cstdint>
#include <vector>

#include "formats/gim.hpp"

namespace harbinger::render {

// RLE sprites reserve palette entries 0 and 1 for two marker colours: pure
// green (painted where a shadow falls) and pure red (a one-pixel rim around
// the figure). Their meaning is still a guess (see gim.md): green as the
// shadow and red as the hover-highlight outline. Raw (non-RLE) paletted images
// use pure green in entries 0-1 as a transparent colour key in every mode but Raw.
enum class KeyMode {
    Shadow,       // green as translucent black, red hidden (current best guess)
    Highlight,    // as Shadow, but draw the red rim (object under the cursor)
    Transparent,  // drop both
    Raw,          // draw the palette colours as stored (for inspection)
};

// RGBA8888 pixels, R in the lowest byte (SDL_PIXELFORMAT_ABGR8888 on little endian).
struct RgbaImage {
    int width = 0, height = 0;
    std::vector<uint32_t> pixels;
};

constexpr uint32_t packRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
}

// `paletteAlpha` applies encoding 7's per-entry alpha. Interface art is drawn
// without it: the original shows the Options slider track (alpha 0x80/0xC0)
// fully opaque.
std::vector<uint32_t> toRgba(const gim::Image& img, size_t frame, KeyMode mode = KeyMode::Shadow,
                             bool paletteAlpha = true);

}  // namespace harbinger::render
