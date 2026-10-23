#include "formats/fnt.hpp"

#include <algorithm>
#include <stdexcept>

#include "formats/binary.hpp"

namespace harbinger::fnt {

const Glyph* Font::glyph(char c) const {
    const int i = static_cast<unsigned char>(c) - 0x20;
    if (i < 0 || i >= 96 || glyphs[size_t(i)].width == 0) return nullptr;
    return &glyphs[size_t(i)];
}

int Font::kern(char previous, char c, bool first) const {
    const int p = static_cast<unsigned char>(previous) - 0x20, i = static_cast<unsigned char>(c) - 0x20;
    if (first || p < 0 || p >= 96 || i < 0 || i >= 96) return 0;
    const int k = kerning[size_t(i * 96 + p)];
    return k == -128 ? defaultKerning : k;
}

gim::Frame Font::decodeGlyph(const Glyph& g) const {
    gim::Frame f;
    if (g.width <= 0 || g.height <= 0) return f;
    if (rle) {
        size_t pos = g.offset;
        gim::decodeRleRows(pixels.data(), pixels.size(), pos, uint16_t(g.width), uint16_t(g.height), f);
        return f;
    }
    if (g.pitch < g.width || size_t(g.offset) + size_t(g.pitch) * size_t(g.height) > pixels.size())
        throw std::runtime_error("fnt: glyph outside pixel data");
    f.index.reserve(size_t(g.width) * size_t(g.height));
    for (int y = 0; y < g.height; ++y) {
        const auto row = pixels.begin() + long(g.offset) + long(y) * g.pitch;
        f.index.insert(f.index.end(), row, row + g.width);
    }
    f.opaque.resize(f.index.size());
    std::transform(f.index.begin(), f.index.end(), f.opaque.begin(), [](uint8_t i) { return uint8_t(i != 0); });
    return f;
}

std::vector<Placed> layout(const Font& font, std::string_view text, int* width) {
    std::vector<Placed> out;
    int pen = 0;
    char previous = 0;
    for (char c : text) {
        const Glyph* g = font.glyph(c);
        if (!g) continue;  // the engine skips zero-width glyphs without touching the pen or kerning state
        const int x = pen + font.kern(previous, c, out.empty()) - g->left;
        out.push_back({g, x});
        pen = x + g->width;
        previous = c;
    }
    if (width) *width = pen;
    return out;
}

Font parse(const std::vector<uint8_t>& data) {
    ByteReader r(data, "fnt");
    Font font;
    for (auto& g : font.glyphs) {
        g.width = r.i16();
        g.height = r.i16();
        g.pitch = r.i16();
        g.top = r.i16();
        g.left = r.i16();
        g.offset = r.u32();
        font.ascent = std::max(font.ascent, g.top);
    }
    font.pixels = r.bytes(r.u32());
    const int16_t colours = r.i16();
    if (colours < 0) r.fail("negative palette size");
    font.palette.resize(size_t(colours));
    for (auto& c : font.palette) {  // stored B, G, R, A like .gim palettes
        c.b = r.u8();
        c.g = r.u8();
        c.r = r.u8();
        c.a = r.u8();
    }
    for (auto& k : font.kerning) k = int8_t(r.u8());
    font.lineHeight = r.i16();
    font.defaultKerning = int8_t(r.u8());
    font.rle = r.u8() != 0;
    r.expectEnd();
    return font;
}

}  // namespace harbinger::fnt
