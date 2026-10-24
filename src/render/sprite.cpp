#include "render/sprite.hpp"

#include <stdexcept>

namespace harbinger::render {

std::vector<uint32_t> toRgba(const gim::Image& img, size_t frame, KeyMode mode, bool paletteAlpha) {
    if (frame >= img.frames.size()) throw std::out_of_range("gim frame");
    const gim::Frame& f = img.frames[frame];
    // Paletted RLE images carry the marker colours, always in entries 0-1. Raw
    // paletted images (cursor frames, portraits) use pure green as a plain
    // colour key: the original draws them without it.
    const bool raw = !img.isRle();
    auto marker = [&](uint8_t idx) -> int {  // 1 = shadow green, 2 = rim red, 3 = raw key, 0 = none
        if (idx > 1 || idx >= img.palette.size()) return 0;
        const gim::Color& c = img.palette[idx];
        if (c.r == 0 && c.g == 255 && c.b == 0) return raw ? 3 : 1;
        if (c.r == 255 && c.g == 0 && c.b == 0 && !raw) return 2;
        return 0;
    };
    // Encoding 7 palettes carry per-entry alpha (guessed meaning, see gim.md).
    paletteAlpha = paletteAlpha && img.encoding() == 7;

    std::vector<uint32_t> out(f.index.size(), 0);
    if (mode == KeyMode::Agent && img.palette.size() > 5) {
        for (size_t i = 0; i < out.size(); ++i) {
            if (!f.opaque[i]) continue;
            const uint8_t idx = f.index[i];
            if (idx <= 5) {
                if (const int a = agentMarkerAlpha(img, idx)) out[i] = packRgba(0, 0, 0, uint8_t(a * 96 / 255));
                continue;
            }
            const gim::Color& c = img.palette[idx];
            out[i] = packRgba(c.r, c.g, c.b, paletteAlpha ? c.a : 255);
        }
        return out;
    }
    for (size_t i = 0; i < out.size(); ++i) {
        if (!f.opaque[i]) continue;
        const uint8_t idx = f.index[i];
        if (const int m = marker(idx); m && mode != KeyMode::Raw) {
            if (m == 1 && (mode == KeyMode::Shadow || mode == KeyMode::Highlight)) out[i] = packRgba(0, 0, 0, 96);
            if (m == 2 && mode == KeyMode::Highlight) out[i] = packRgba(255, 64, 64, 255);
            continue;
        }
        if (idx < img.palette.size()) {
            const gim::Color& c = img.palette[idx];
            out[i] = packRgba(c.r, c.g, c.b, paletteAlpha ? c.a : 255);
        } else {
            out[i] = packRgba(idx, idx, idx, 255);  // unpaletted (kind 3) images: show as grey
        }
    }
    return out;
}

int agentMarkerAlpha(const gim::Image& img, uint8_t index) {
    if (index == 0 || index > 5 || img.palette.size() <= 5) return 0;
    const gim::Color& shadow = img.palette[1];
    const gim::Color& c = img.palette[index];
    return shadow.r >= shadow.g ? c.r : c.g;
}

}  // namespace harbinger::render
