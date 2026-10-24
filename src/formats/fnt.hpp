// Bitmap fonts (.fnt) and the engine's text layout rules.
// Format notes and evidence: docs/formats/fnt.md
#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "formats/gim.hpp"

namespace harbinger::fnt {

// Characters 0x20..0x7F; a glyph with width 0 is not drawn and takes no space.
struct Glyph {
    int16_t width = 0, height = 0, pitch = 0;
    int16_t top = 0;   // rows above the baseline
    int16_t left = 0;  // pulled back from the pen position before drawing
    uint32_t offset = 0;  // into Font::pixels
};

struct Font {
    std::array<Glyph, 96> glyphs;
    std::vector<uint8_t> pixels;
    std::vector<gim::Color> palette;  // index 0 is the transparent key
    std::array<int8_t, 96 * 96> kerning{};  // [ch * 96 + previous]; -128 = use defaultKerning
    int16_t lineHeight = 0;
    int8_t defaultKerning = 0;
    bool rle = false;  // glyph pixels use .gim RLE rows instead of raw rows
    int16_t ascent = 0;  // largest Glyph::top, the engine's baseline offset

    const Glyph* glyph(char c) const;
    // Extra horizontal spacing between `previous` and `c` (0 for the first character).
    int kern(char previous, char c, bool first) const;
    // Palette indices of a glyph, width x height, row-major.
    gim::Frame decodeGlyph(const Glyph& g) const;
};

struct Placed {
    const Glyph* glyph;
    int x;  // left edge relative to the string origin
};

// Lays out one line as the engine does (FUN_00514550 / FUN_00513c20).
std::vector<Placed> layout(const Font& font, std::string_view text, int* width = nullptr);

// Throws std::runtime_error on malformed data or trailing bytes.
Font parse(const std::vector<uint8_t>& data);

}  // namespace harbinger::fnt
