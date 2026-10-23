#include "ui/text.hpp"

namespace harbinger::ui {

render::RgbaImage renderTextBox(const fnt::Font& font, const std::vector<gim::Color>& palette,
                                const itf::TextBox& box, std::string_view text) {
    render::RgbaImage out;
    out.width = box.width;
    out.height = box.height;
    out.pixels.assign(size_t(out.width) * size_t(out.height), 0);

    int textWidth = 0;
    const auto placed = fnt::layout(font, text, &textWidth);
    // GCtrlStaticTextBox_v3 picks the origin, FUN_00513c20 applies the alignment.
    int x = box.padX;
    if (box.align == 0x10) x = box.padX + box.width - textWidth;
    if (box.align == 0x20) x = box.width / 2 - textWidth / 2;
    const int baseline = font.ascent + box.padY;

    for (const auto& p : placed) {
        const gim::Frame f = font.decodeGlyph(*p.glyph);
        const int gx = x + p.x, gy = baseline - p.glyph->top;
        for (int row = 0; row < p.glyph->height; ++row) {
            const int y = gy + row;
            if (y < 0 || y >= out.height) continue;
            for (int col = 0; col < p.glyph->width; ++col) {
                const int px = gx + col;
                const size_t src = size_t(row) * size_t(p.glyph->width) + size_t(col);
                if (px < 0 || px >= out.width || !f.opaque[src]) continue;
                const uint8_t idx = f.index[src];
                uint32_t& dst = out.pixels[size_t(y) * size_t(out.width) + size_t(px)];
                if (idx == 1) dst = kGlyphFringe;
                else if (idx < palette.size()) dst = render::packRgba(palette[idx].r, palette[idx].g, palette[idx].b, 255);
            }
        }
    }
    return out;
}

}  // namespace harbinger::ui
