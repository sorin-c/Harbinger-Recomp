// Tests for the front-end UI pieces: text boxes and animated item stepping.
#include <cstdio>
#include <vector>

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

int main() {
    testAnimation();
    testTextBox();
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
