// Text drawing for interface text boxes (GCtrlStaticTextBox).
#pragma once

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

// Draws one line of text into a transparent image the size of the box, placed
// as GCtrlStaticTextBox does: baseline at font.ascent + padY; left-aligned text
// starts at padX, right-aligned text ends at padX + width, centred text is
// centred on width / 2.
// Glyph palette indices are coloured from `palette`, which the engine copies
// from the interface's background image. Index 0 is transparent.
render::RgbaImage renderTextBox(const fnt::Font& font, const std::vector<gim::Color>& palette,
                                const itf::TextBox& box, std::string_view text);

}  // namespace harbinger::ui
