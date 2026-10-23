// Decoder for Harbinger's .gim images (sprites, tiles, UI art, save thumbnails).
// Format notes and evidence: docs/formats/gim.md
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace harbinger::gim {

struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 0;
};

// One decoded frame: palette indices, row-major, `width` per row.
// Transparent pixels (skipped by the RLE) are marked in `opaque`.
struct Frame {
    std::vector<uint8_t> index;
    std::vector<uint8_t> opaque;  // 1 = pixel present, 0 = transparent
};

struct Image {
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t pitch = 0;      // row stride of raw (uncompressed) payloads
    uint8_t kind = 0;        // byte 6 of the header; 1 for every paletted image
    uint8_t flags = 0;       // byte 7: low nibble = encoding, 0x10 = has pixels, 0x20 = has palette
    int16_t originX = 0;     // bytes 8..11, meaning not yet confirmed
    int16_t originY = 0;
    std::vector<Color> palette;
    uint8_t extra[8] = {};   // present when (encoding & 3) != 0, meaning not yet confirmed
    std::vector<Frame> frames;

    uint8_t encoding() const { return flags & 0x0F; }
    bool isRle() const { return encoding() & 0x04; }
};

// Decodes `height` RLE rows of `width` pixels starting at d[pos] and advances
// `pos`. Also used by RLE fonts (.fnt), which share the row encoding.
void decodeRleRows(const uint8_t* d, size_t size, size_t& pos, uint16_t width, uint16_t height, Frame& f);

// Throws std::runtime_error when the data is malformed or not fully consumed.
Image decode(const std::vector<uint8_t>& data);

}  // namespace harbinger::gim
