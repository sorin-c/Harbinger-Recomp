#include "ui/text.hpp"

#include <algorithm>

namespace harbinger::ui {

TextMetrics measure(const fnt::Font& font, std::string_view text) {
    TextMetrics m;
    int below = 0;
    for (const auto& p : fnt::layout(font, text, &m.width)) {
        m.ascent = std::max<int>(m.ascent, p.glyph->top);
        below = std::max<int>(below, p.glyph->height - p.glyph->top);
    }
    m.height = m.ascent + below;
    return m;
}

std::vector<std::string> wrapText(const fnt::Font& font, std::string_view text, int width) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t lastBreak = start;  // end of the words that are known to fit
        bool haveBreak = false;
        size_t end;
        for (size_t from = start;;) {
            end = from;
            while (end < text.size() && text[end] >= '!') ++end;
            const bool last = end == text.size() || text[end] != ' ';
            if (measure(font, text.substr(start, end - start)).width > width) {
                if (haveBreak) end = lastBreak;
                break;
            }
            if (last) break;
            lastBreak = end;
            haveBreak = true;
            from = end + 1;
        }
        lines.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

void drawText(render::RgbaImage& dst, const fnt::Font& font, const std::vector<gim::Color>& palette,
              std::string_view text, int x, int baseline, uint16_t align) {
    int textWidth = 0;
    const auto placed = fnt::layout(font, text, &textWidth);
    // FUN_00513c20 applies the alignment to the origin.
    if (align == 0x10) x -= textWidth;
    if (align == 0x20) x -= textWidth / 2;

    for (const auto& p : placed) {
        const gim::Frame f = font.decodeGlyph(*p.glyph);
        const int gx = x + p.x, gy = baseline - p.glyph->top;
        for (int row = 0; row < p.glyph->height; ++row) {
            const int y = gy + row;
            if (y < 0 || y >= dst.height) continue;
            for (int col = 0; col < p.glyph->width; ++col) {
                const int px = gx + col;
                const size_t src = size_t(row) * size_t(p.glyph->width) + size_t(col);
                if (px < 0 || px >= dst.width || !f.opaque[src]) continue;
                const uint8_t idx = f.index[src];
                uint32_t& out = dst.pixels[size_t(y) * size_t(dst.width) + size_t(px)];
                if (idx == 1) out = kGlyphFringe;
                else if (idx < palette.size()) out = render::packRgba(palette[idx].r, palette[idx].g, palette[idx].b, 255);
            }
        }
    }
}

namespace {

render::RgbaImage blank(int w, int h) {
    render::RgbaImage img;
    img.width = w;
    img.height = h;
    img.pixels.assign(size_t(w) * size_t(h), 0);
    return img;
}

void blit(render::RgbaImage& dst, const render::RgbaImage& src) {
    for (int y = 0; y < std::min(dst.height, src.height); ++y)
        for (int x = 0; x < std::min(dst.width, src.width); ++x) {
            const uint32_t p = src.pixels[size_t(y) * size_t(src.width) + size_t(x)];
            if (p >> 24) dst.pixels[size_t(y) * size_t(dst.width) + size_t(x)] = p;
        }
}

}  // namespace

render::RgbaImage renderTextBox(const BoxFonts& fonts, const std::vector<gim::Color>& palette,
                                const itf::TextBox& box, std::string_view text, int highlight,
                                const render::RgbaImage* background) {
    // FUN_00466a90: the box grows to fit its image.
    const int width = std::max<int>(box.width, background ? background->width : 0);
    const int height = std::max<int>(box.height, background ? background->height : 0);
    render::RgbaImage out = blank(width, height);
    if (background) blit(out, *background);

    const fnt::Font& normal = *fonts.normal;
    if (normal.lineHeight <= 0) return out;
    // GCtrlStaticTextBox_v3: the origin, then one line per lineHeight.
    int x = box.padX;
    if (box.align == 0x10) x = box.padX + width;
    if (box.align == 0x20) x = width / 2;
    const int visible = height / normal.lineHeight;
    const auto lines = wrapText(normal, text, width);
    for (int n = 0; n < int(lines.size()) && n < visible; ++n) {
        std::string_view line = lines[size_t(n)];
        const fnt::Font* font = &normal;
        if (!line.empty() && line[0] == '*') font = fonts.star, line.remove_prefix(1);
        if (!line.empty() && line[0] == '&') font = fonts.alt, line.remove_prefix(1);
        if (n == highlight) font = fonts.alt;
        drawText(out, *font, palette, line, x, normal.ascent + box.padY + n * normal.lineHeight, box.align);
    }
    return out;
}

render::RgbaImage renderTextBox(const fnt::Font& font, const std::vector<gim::Color>& palette,
                                const itf::TextBox& box, std::string_view text) {
    return renderTextBox(BoxFonts{&font, &font, &font}, palette, box, text);
}

render::RgbaImage renderTextButton(const render::RgbaImage& face, const fnt::Font& font,
                                   const std::vector<gim::Color>& palette, std::string_view caption) {
    render::RgbaImage out = face;
    const TextMetrics m = measure(font, caption);
    drawText(out, font, palette, caption, face.width / 2, (face.height - m.height) / 2 - 1 + font.ascent, 0x20);
    return out;
}

}  // namespace harbinger::ui
