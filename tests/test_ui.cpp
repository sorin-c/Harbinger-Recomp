// Tests for the front-end UI pieces: text boxes and animated item stepping.
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/settings.hpp"
#include "ui/controls.hpp"
#include "ui/item_animation.hpp"
#include "ui/text.hpp"

using namespace harbinger;
using State = ui::ItemAnimation::State;

static int failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

static std::vector<int> run(ui::ItemAnimation& a, int steps) {
    std::vector<int> out;
    for (int i = 0; i < steps; ++i) {
        a.step();
        out.push_back(a.frame());
    }
    return out;
}

static void testAnimation() {
    const itm::Animation marks{2, 5, 7};
    ui::ItemAnimation a(marks, 9);
    a.setState(State::Hover);  // loops over [2, 5)
    CHECK(run(a, 6) == (std::vector<int>{1, 2, 3, 4, 2, 3}));
    a.setState(State::Idle);  // finish the loop, jump below it, rewind to 0
    CHECK(run(a, 6) == (std::vector<int>{4, 1, 0, 0, 0, 0}));
    a.setState(State::Pressed);  // skip the hover loop, run to the end, then hold near pressEnd
    CHECK(run(a, 9) == (std::vector<int>{1, 5, 6, 7, 8, 7, 8, 7, 8}));
    a.setState(State::Idle);  // from beyond pressEnd: wrap to pressEnd - 1, then rewind
    CHECK(run(a, 6) == (std::vector<int>{6, 5, 1, 0, 0, 0}));
    a.setState(State::Hover);
    run(a, 3);
    a.setState(State::Reset);
    a.step();
    CHECK(a.frame() == 0 && a.state() == State::Idle);

    // One step per update, and only after more than 100 ms.
    ui::ItemAnimation t(marks, 9);
    t.setState(State::Hover);
    t.update(1000);
    t.update(1100);
    CHECK(t.frame() == 0);
    t.update(1101);
    CHECK(t.frame() == 1);
    t.update(1150);
    CHECK(t.frame() == 1);
    t.update(5000);  // a long pause still advances a single frame
    CHECK(t.frame() == 2);
}

static void testTextBox() {
    fnt::Font font;
    font.ascent = 3;
    font.lineHeight = 4;
    font.glyphs['I' - 0x20] = {2, 3, 2, 3, 0, 0};
    font.pixels = {2, 1, 2, 1, 2, 0};
    font.kerning.fill(-128);
    font.defaultKerning = 1;
    std::vector<gim::Color> palette = {{0, 255, 0, 255}, {9, 9, 9, 255}, {10, 20, 30, 255}};

    itf::TextBox box;
    box.width = 10;
    box.height = 4;
    box.align = 0x20;
    box.padY = 1;
    const auto img = ui::renderTextBox(font, palette, box, "II");  // 5 px wide, centred at 5 - 2 = 3
    CHECK(img.width == 10 && img.height == 4);
    auto at = [&](int x, int y) { return img.pixels[size_t(y * img.width + x)]; };
    const uint32_t ink = render::packRgba(10, 20, 30, 255);
    CHECK(at(2, 1) == 0 && at(3, 1) == ink && at(4, 1) == ui::kGlyphFringe);
    CHECK(at(6, 1) == ink && at(7, 1) == ui::kGlyphFringe && at(8, 1) == 0);
    CHECK(at(4, 3) == 0 && at(3, 0) == 0);  // index 0 transparent; baseline = ascent + padY

    box.align = 0;
    box.padX = 1;
    CHECK(ui::renderTextBox(font, palette, box, "I").pixels[size_t(1 * 10 + 1)] == ink);
    box.align = 0x10;  // right-aligned text ends at padX + width (clipped here)
    CHECK(ui::renderTextBox(font, palette, box, "I").pixels[size_t(1 * 10 + 9)] == ink);
}

// A font whose every printable glyph is a 2x3 block (1 px below the
// baseline) advancing 3 px: default kerning 1.
static fnt::Font blockFont() {
    fnt::Font font;
    font.ascent = 2;
    font.lineHeight = 5;
    font.pixels = {2, 2, 2, 2, 2, 2};
    for (char c = '!'; c < 0x7F; ++c) font.glyphs[size_t(c - 0x20)] = {2, 3, 2, 2, 0, 0};
    font.kerning.fill(-128);
    font.defaultKerning = 1;
    return font;
}

static void testWrap() {
    const fnt::Font font = blockFont();
    // "ab" is 5 px, "ab cd" 11 px (space has no glyph and takes no room).
    CHECK(ui::measure(font, "ab").width == 5);
    CHECK(ui::measure(font, "ab").height == 3 && ui::measure(font, "ab").ascent == 2);
    using Lines = std::vector<std::string>;
    CHECK(ui::wrapText(font, "ab cd", 11) == (Lines{"ab cd"}));
    CHECK(ui::wrapText(font, "ab cd", 10) == (Lines{"ab", "cd"}));
    CHECK(ui::wrapText(font, "ab cd ef", 10) == (Lines{"ab", "cd", "ef"}));
    CHECK(ui::wrapText(font, "abcdef gh", 4) == (Lines{"abcdef", "gh"}));  // a long word stays whole
    CHECK(ui::wrapText(font, "ab\ncd", 50) == (Lines{"ab", "cd"}));        // control characters end a line
    CHECK(ui::wrapText(font, "", 50).empty());

    // Only lines starting inside the box are drawn: "Swap Mouse Buttons"
    // loses its second line in a box one line high.
    std::vector<gim::Color> palette = {{0, 255, 0, 255}, {9, 9, 9, 255}, {10, 20, 30, 255}};
    itf::TextBox box;
    box.width = 10;
    box.height = 5;
    const auto one = ui::renderTextBox(font, palette, box, "ab cd");
    const uint32_t ink = render::packRgba(10, 20, 30, 255);
    CHECK(one.pixels[size_t(0 * 10 + 0)] == ink);
    box.height = 10;
    const auto two = ui::renderTextBox(font, palette, box, "ab cd");
    CHECK(two.pixels[size_t(5 * 10 + 0)] == ink);  // second baseline = 2 + lineHeight 5
    // A box grows to its background image.
    render::RgbaImage bg{12, 6, std::vector<uint32_t>(72, render::packRgba(1, 2, 3, 255))};
    box.height = 5;
    const auto grown = ui::renderTextBox(ui::BoxFonts{&font, &font, &font}, palette, box, "", -1, &bg);
    CHECK(grown.width == 12 && grown.height == 6 && grown.pixels[71] == bg.pixels[0]);

    // TextBtn: caption centred, baseline (h - textHeight) / 2 - 1 + ascent.
    render::RgbaImage face{20, 9, std::vector<uint32_t>(180, 0)};
    const auto btn = ui::renderTextButton(face, font, palette, "ab");  // 5 px wide at 10 - 2 = 8
    // baseline (9 - 3) / 2 - 1 + 2 = 4, so the glyphs cover rows 2..4
    CHECK(btn.pixels[size_t(2 * 20 + 8)] == ink && btn.pixels[size_t(1 * 20 + 8)] == 0);
    CHECK(btn.pixels[size_t(4 * 20 + 12)] == ink && btn.pixels[size_t(4 * 20 + 13)] == 0);
}

static void testControls() {
    using E = ui::PointerEvent;
    using S = ui::ButtonState;
    ui::Button b;
    CHECK(!b.event(E::Down) && b.state == S::Pressed);
    CHECK(!b.event(E::Cancel) && b.state == S::Normal);  // released elsewhere: no click
    b.event(E::Down);
    CHECK(b.event(E::Up) && b.state == S::Normal);
    b.state = b.saved = S::Disabled;
    CHECK(!b.event(E::Down) && !b.event(E::Up) && b.state == S::Disabled);

    ui::CheckBox c;
    CHECK(c.image() == 1);  // off
    c.event(E::Down);
    CHECK(c.image() == 0);  // pressing previews "on"
    CHECK(c.event(E::Up) && c.checked && c.state == S::Checked && c.image() == 0);
    c.event(E::Down);
    CHECK(c.state == S::Checked);  // no pressed look while checked
    CHECK(c.event(E::Up) && !c.checked && c.state == S::Normal);
    c.set(true);
    CHECK(c.checked && c.image() == 0);

    ui::SliderBar s;  // 400 wide, 25 px arrows: 350 steps of track
    s.set(0.5);
    CHECK(s.pos == 175 && s.changed() < 0);
    CHECK(s.event(E::Down, 10, 0) && s.pos == 172 && s.leftHeld);  // left arrow: 3 px
    CHECK(s.event(E::Held, 10, 20) && s.pos == 172);                // repeats only after 25 ms
    CHECK(s.event(E::Held, 10, 26) && s.pos == 169);
    s.event(E::Up, 10, 30);
    CHECK(!s.leftHeld && s.changed() == 169.0 / 350);
    s.event(E::Down, 100, 40);  // on the track: snaps to x - x % 3 - 25
    CHECK(s.pos == 74 && !s.leftHeld && !s.rightHeld);
    s.event(E::Down, 5, 40);
    s.event(E::Up, 5, 40);
    s.pos = 349;
    s.event(E::Down, 390, 50);  // right arrow, clamped to the track
    CHECK(s.pos == 350 && s.rightHeld && s.value() == 1.0);
    s.event(E::Cancel, 390, 50);
    s.nudge(-1);
    CHECK(s.pos == 332);
}

static void testSettings() {
    Settings s;
    s.music = 0.25;
    s.keys[0] = 0x2E;
    s.altKeys[20] = 0x36;
    s.swapMouse = true;
    const auto bytes = s.serialize();
    CHECK(bytes.size() == 67);
    const Settings t = Settings::parse(bytes);
    CHECK(t.music == 0.25 && t.sound == 1.0 && t.gamma == 0.5 && t.keys[0] == 0x2E && t.altKeys[20] == 0x36 && t.swapMouse);
    CHECK(t.serialize() == bytes);
    CHECK(s.musicGain() == 0.25 / 12 && Settings().displayGamma() == 1.0);
    bool threw = false;
    try {
        Settings::parse(std::vector<uint8_t>(66));
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

int main() {
    testAnimation();
    testTextBox();
    testWrap();
    testControls();
    testSettings();
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
