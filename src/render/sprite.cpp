#include "render/sprite.hpp"

#include <stdexcept>

namespace harbinger::render {
namespace {

constexpr uint32_t pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
}

}  // namespace

std::vector<uint32_t> toRgba(const gim::Image& img, size_t frame, KeyMode mode) {
    if (frame >= img.frames.size()) throw std::out_of_range("gim frame");
    const gim::Frame& f = img.frames[frame];
    // Only paletted RLE images carry the marker colours, always in entries 0-1.
    const bool keyed = img.isRle() && !img.palette.empty();
    auto marker = [&](uint8_t idx) -> int {  // 1 = shadow green, 2 = rim red, 0 = none
        if (!keyed || idx > 1 || idx >= img.palette.size()) return 0;
        const gim::Color& c = img.palette[idx];
        if (c.r == 0 && c.g == 255 && c.b == 0) return 1;
        if (c.r == 255 && c.g == 0 && c.b == 0) return 2;
        return 0;
    };
    // Encoding 7 palettes carry per-entry alpha (guessed meaning, see gim.md).
    const bool paletteAlpha = img.encoding() == 7;

    std::vector<uint32_t> out(f.index.size(), 0);
    for (size_t i = 0; i < out.size(); ++i) {
        if (!f.opaque[i]) continue;
        const uint8_t idx = f.index[i];
        if (const int m = marker(idx); m && mode != KeyMode::Raw) {
            if (m == 1 && (mode == KeyMode::Shadow || mode == KeyMode::Highlight)) out[i] = pack(0, 0, 0, 96);
            if (m == 2 && mode == KeyMode::Highlight) out[i] = pack(255, 64, 64, 255);
            continue;
        }
        if (idx < img.palette.size()) {
            const gim::Color& c = img.palette[idx];
            out[i] = pack(c.r, c.g, c.b, paletteAlpha ? c.a : 255);
        } else {
            out[i] = pack(idx, idx, idx, 255);  // unpaletted (kind 3) images: show as grey
        }
    }
    return out;
}

}  // namespace harbinger::render
