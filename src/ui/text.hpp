// Text drawing for interface controls (GCtrlStaticTextBox, GCtrlTextBtn).
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "formats/fnt.hpp"
#include "formats/gim.hpp"
#include "formats/itf.hpp"
#include "render/sprite.hpp"

namespace harbinger::ui {

// Colour of glyph pixels with palette index 1, the dark anti-aliasing fringe.
// Observed in the original as a constant (4,4,4) whatever lies behind it; the
// code that produces it has not been found yet (docs/formats/fnt.md).
constexpr uint32_t kGlyphFringe = render::packRgba(4, 4, 4, 255);

// Size of one line of text as FUN_00514550 measures it.
struct TextMetrics {
    int width = 0;
    int ascent = 0;  // largest glyph top in this text
    int height = 0;  // ascent + the deepest glyph bottom below the baseline
};
TextMetrics measure(const fnt::Font& font, std::string_view text);

// Splits text into the lines a box `width` pixels wide shows (FUN_00466900):
// words are added while the line still fits, a too-long first word is kept
// whole, and any character below '!' other than a space ends the line.
std::vector<std::string> wrapText(const fnt::Font& font, std::string_view text, int width);

// Draws one line with its baseline at `baseline`. `align` is the itf value:
// 0 = text starts at x, 0x10 = text ends at x, 0x20 = text centred on x.
// Glyph palette indices are coloured from `palette`, which the engine copies
// from the interface's background image. Index 0 is transparent.
void drawText(render::RgbaImage& dst, const fnt::Font& font, const std::vector<gim::Color>& palette,
              std::string_view text, int x, int baseline, uint16_t align);

// The three fonts of a text box block.
struct BoxFonts {
    const fnt::Font* normal = nullptr;
    const fnt::Font* alt = nullptr;   // lines starting with '&', and the highlighted line
    const fnt::Font* star = nullptr;  // lines starting with '*'
};

// Draws a text box as GCtrlStaticTextBox_v3 does, into a transparent image
// the size of the box (or of `background`, if that is larger; it is drawn
// first). The text is wrapped to the box width; line n has its baseline at
// normal.ascent + padY + n * normal.lineHeight, and only the lines that start
// inside the box are drawn. Left-aligned lines start at padX, right-aligned
// lines end at padX + width, centred lines are centred on width / 2.
// `highlight` is the line drawn with the alt font, or -1.
render::RgbaImage renderTextBox(const BoxFonts& fonts, const std::vector<gim::Color>& palette,
                                const itf::TextBox& box, std::string_view text, int highlight = -1,
                                const render::RgbaImage* background = nullptr);

// Single-font form, for boxes whose three fonts are the same.
render::RgbaImage renderTextBox(const fnt::Font& font, const std::vector<gim::Color>& palette,
                                const itf::TextBox& box, std::string_view text);

// A TextBtn face (GCtrlTextBtn_v3): `face` with the caption centred on it,
// baseline at (h - text height) / 2 - 1 + font ascent.
render::RgbaImage renderTextButton(const render::RgbaImage& face, const fnt::Font& font,
                                   const std::vector<gim::Color>& palette, std::string_view caption);

}  // namespace harbinger::ui
