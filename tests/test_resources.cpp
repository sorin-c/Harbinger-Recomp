// Tests for the .mgr, .itf, .fnt, .acr and .itm parsers. Synthetic cases always
// run; with HARBINGER_DATA set, the real files must parse completely.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/vfs.hpp"
#include "formats/acr.hpp"
#include "formats/fnt.hpp"
#include "formats/gim.hpp"
#include "formats/itf.hpp"
#include "formats/itm.hpp"
#include "formats/mgr.hpp"

using namespace harbinger;

static int failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

template <typename F>
static bool throws(F f) {
    try {
        f();
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

// Little-endian byte builder for synthetic files.
struct Bytes {
    std::vector<uint8_t> d;
    Bytes& u8(uint8_t v) { d.push_back(v); return *this; }
    Bytes& u16(uint16_t v) { return u8(uint8_t(v)).u8(uint8_t(v >> 8)); }
    Bytes& u32(uint32_t v) { return u16(uint16_t(v)).u16(uint16_t(v >> 16)); }
    Bytes& str(const std::string& s) {  // engine string: length without the NUL
        u32(uint32_t(s.size()));
        d.insert(d.end(), s.begin(), s.end());
        return u8(0);
    }
    Bytes& strNul(const std::string& s) {  // .mgr paths: length counts the NUL
        u32(uint32_t(s.size() + 1));
        d.insert(d.end(), s.begin(), s.end());
        return u8(0);
    }
};

static void testMgr() {
    Bytes b;
    b.u32(2).u16(5).u16(7).strNul("a\\one.itm").strNul("b\\two.itm").str("Items.itm");
    const auto idx = mgr::parse(b.d);
    CHECK(idx.name == "Items.itm");
    CHECK(idx.entries.size() == 2);
    CHECK(idx.find(7) && *idx.find(7) == "b\\two.itm");
    CHECK(idx.find(6) == nullptr);
    b.u8(0);
    CHECK(throws([&] { mgr::parse(b.d); }));

    // Negative count: a skipped word follows (speech, dialog, datacard).
    Bytes n;
    n.u32(uint32_t(-1)).u16(50000).u16(50000).strNul("v.mp3").str("..");
    const auto neg = mgr::parse(n.d);
    CHECK(neg.entries.size() == 1 && neg.find(50000) && *neg.find(50000) == "v.mp3");
}

static void testItf() {
    Bytes b;
    b.u32(0).u32(460).u32(0).u16(3).u16(15);
    b.u16(itf::Backgd).u16(0).u16(0).u16(0).str("Background").u32(1).str("bg.tga").str("");
    b.u16(itf::StaticTextBox).u16(32).u16(40).u16(0).str("Label").u32(0).str("");
    b.u16(244).u16(32).u16(15).u16(14).u16(13).u16(0x20).u16(1).u16(2).str("New Game");
    b.u16(itf::AnimatedBtn).u16(28).u16(60).u16(uint16_t(-1000)).str("Button").u32(0).str("");
    b.u16(743).u32(4).str("");
    b.u8(1).u16(3).u16(0).u16(373).u16(374);
    const auto itf = itf::parse(b.d);
    CHECK(itf.header[1] == 460 && itf.kind == 15);
    CHECK(itf.controls.size() == 3);
    CHECK(itf.controls[0].images.size() == 1 && itf.controls[0].images[0] == "bg.tga");
    const auto& label = itf.controls[1];
    CHECK(label.textBox && label.textBox->width == 244 && label.textBox->font == 15 &&
          label.textBox->fontAlt == 14 && label.textBox->fontStar == 13 && label.textBox->align == 0x20 &&
          label.textBox->padX == 1 && label.textBox->padY == 2);
    CHECK(label.text == "New Game");
    const auto& button = itf.controls[2];
    CHECK(button.z == -1000 && button.values.size() == 2 && button.values[0] == 743 && button.values[1] == 4);
    CHECK(itf.sounds.size() == 3 && itf.sounds[2] == 374);
    CHECK(std::strcmp(itf::className(0x50), "AnimatedBtn") == 0);
    CHECK(std::strcmp(itf::className(0x56), "QuestLogButton") == 0);
    CHECK(itf::className(0x57) == nullptr);

    Bytes bad;
    bad.u32(0).u32(0).u32(0).u16(1).u16(0).u16(0x57).u16(0).u16(0).u16(0).str("x").u32(0).str("").u8(0);
    CHECK(throws([&] { itf::parse(bad.d); }));
}

// A three-glyph font: 'A' 2x2, 'B' 3x1 pulled back by 1, ' ' 4 wide and empty.
static Bytes syntheticFont(bool kernBA) {
    Bytes b;
    for (int c = 0x20; c < 0x80; ++c) {
        if (c == 'A') b.u16(2).u16(2).u16(4).u16(2).u16(0).u32(0);
        else if (c == 'B') b.u16(3).u16(1).u16(4).u16(1).u16(1).u32(8);
        else if (c == ' ') b.u16(4).u16(0).u16(4).u16(0).u16(0).u32(12);
        else b.u16(0).u16(0).u16(0).u16(0).u16(0).u32(0);
    }
    b.u32(12);
    for (uint8_t p : {1, 2, 0, 0, 2, 1, 0, 0, 2, 2, 2, 0}) b.u8(p);
    b.u16(3).u32(0xFF00FF00).u32(0xFF0A0B0C).u32(0xFF102030);  // B, G, R, A order
    for (int ch = 0; ch < 96; ++ch)
        for (int prev = 0; prev < 96; ++prev)
            b.u8(kernBA && ch == 'B' - 0x20 && prev == 'A' - 0x20 ? 5 : 0x80);
    b.u16(10).u8(1).u8(0);
    return b;
}

static void testFnt() {
    const auto font = fnt::parse(syntheticFont(true).d);
    CHECK(font.ascent == 2 && font.lineHeight == 10 && font.defaultKerning == 1 && !font.rle);
    CHECK(font.palette.size() == 3 && font.palette[1].r == 0x0A && font.palette[1].b == 0x0C);
    CHECK(font.glyph('C') == nullptr && font.glyph('A') != nullptr);
    int w = 0;
    auto placed = fnt::layout(font, "AB", &w);
    CHECK(placed.size() == 2 && placed[0].x == 0 && placed[1].x == 2 + 5 - 1 && w == 9);
    placed = fnt::layout(font, "AA", &w);
    CHECK(placed[1].x == 3 && w == 5);  // default kerning of 1
    placed = fnt::layout(font, "ACA", &w);
    CHECK(placed.size() == 2 && placed[1].x == 3);  // missing glyphs take no space
    const auto f = font.decodeGlyph(*font.glyph('A'));
    CHECK(f.index == (std::vector<uint8_t>{1, 2, 2, 1}) && f.opaque == (std::vector<uint8_t>{1, 1, 1, 1}));
    auto b = syntheticFont(false);
    b.u8(0);
    CHECK(throws([&] { fnt::parse(b.d); }));
}

static void testAcr() {
    Bytes b;
    b.u32(10).u16(18).u16(5).u16(2).str("c\\0.tga").str("c\\1.tga");
    const auto c = acr::parse(b.d);
    CHECK(c.frameDelay == 10 && c.hotX == 18 && c.hotY == 5);
    CHECK(c.frames.size() == 2 && c.frames[1] == "c\\1.tga");
}

static void testItm() {
    Bytes b;
    b.u16(itm::AnimatedItem).u16(563).u16(0).u16(295).u16(20).u16(10).u16(10).u32(0).str("New Button");
    b.u8(0).u16(8).u16(19).u16(19).u16(11);
    const auto it = itm::parse(b.d);
    CHECK(it.image == 295 && it.frameCount == 20 && it.name == "New Button");
    CHECK(it.animation && it.animation->loopStart == 8 && it.animation->loopEnd == 19 && it.animation->pressEnd == 19);
    CHECK(it.trailer == 11);
    b.d[0] = 9;  // other classes are not decoded yet
    CHECK(throws([&] { itm::parse(b.d); }));

    // Armour (version 2.2): the optional block, the socketed body, the
    // armour word, an empty list, the trailer.
    Bytes a;
    a.u16(itm::HumanArmor).u16(563).u16(0).u16(2).u16(2).u16(4).u16(3).u32(650).str("Alpha Advance Armor");
    a.u8(1).u32(0xffffffff).u32(0xffffffff).u16(494).u8(0).u16(18).u16(45).u16(0).u16(1).u16(7).u16(9).u16(0);
    const auto armour = itm::parse(a.d);
    CHECK(armour.block && armour.socketed && armour.socketed->animGroup == 18 && armour.socketed->word54 == 45);
    CHECK(armour.socketed->list.size() == 1 && armour.socketed->list[0].second == 9);
    CHECK(itm::spriteLayer(armour.classId) == 0 && itm::spriteLayer(itm::HumanWeapon) == 1 &&
          itm::spriteLayer(itm::GladiatorRanged) == 2 && itm::spriteLayer(itm::LeftGauntlet) == -1);
    // Version 2.1 and below: no list.
    Bytes g;
    g.u16(itm::LeftGauntlet).u16(537).u16(0).u16(87).u16(2).u16(2).u16(4).u32(423).str("Salvaged Left Gauntlet");
    g.u8(0).u16(357).u8(0).u16(1).u16(8);
    const auto gauntlet = itm::parse(g.d);
    CHECK(!gauntlet.block && gauntlet.socketed && gauntlet.socketed->animGroup == 1 && gauntlet.trailer == 8);
}

static bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

static void testRealData(const char* dir) {
    Vfs vfs(dir);
    auto read = [&](const std::string& p) {
        auto d = vfs.read(p);
        if (!d) throw std::runtime_error("missing " + p);
        return *d;
    };

    // Every interface parses to the last byte.
    size_t itfs = 0;
    for (const auto& p : vfs.list("res/common/interface/"))
        if (endsWith(p, ".itf")) {
            ++itfs;
            try {
                itf::parse(read(p));
            } catch (const std::exception& ex) {
                std::fprintf(stderr, "%s: %s\n", p.c_str(), ex.what());
                ++failures;
            }
        }
    CHECK(itfs == 41);

    const auto startup = itf::parse(read("res/common/interface/start/startup.itf"));
    CHECK(startup.kind == 15 && startup.controls.size() == 9);
    CHECK(startup.controls[1].text == "New Game" && startup.controls[1].textBox->font == 15);
    CHECK(startup.controls[8].classId == itf::AnimatedBtn && startup.controls[8].values[0] == 743);

    // Resource indexes, plain and negative-count layouts.
    for (const char* m : {"font", "item", "itemimg", "interface", "cursor", "sound", "weapons", "effects",
                          "hspeech", "gspeech", "cspeech", "hdialog", "gdialog", "cdialog", "datacard"}) {
        try {
            mgr::parse(read(std::string("res/common/managers/") + m + ".mgr"));
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "%s.mgr: %s\n", m, ex.what());
            ++failures;
        }
    }

    // Every font parses and every glyph decodes; the menu label is 241 px wide.
    const auto fonts = mgr::parse(read("res/common/managers/font.mgr"));
    CHECK(fonts.entries.size() == 16);
    for (const auto& [id, path] : fonts.entries) {
        try {
            const auto font = fnt::parse(read(path));
            for (const auto& g : font.glyphs) font.decodeGlyph(g);
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "%s: %s\n", path.c_str(), ex.what());
            ++failures;
        }
    }
    int width = 0;
    fnt::layout(fnt::parse(read(*fonts.find(15))), "New Game", &width);
    CHECK(width == 241);

    // The main menu's New Game button: item -> image strip of 20 square frames.
    const auto items = mgr::parse(read("res/common/managers/item.mgr"));
    const auto images = mgr::parse(read("res/common/managers/itemimg.mgr"));
    CHECK(items.find(743) && endsWith(sqh::normalizePath(*items.find(743)), "buttons/newbutton.itm"));
    for (uint16_t id : {743, 744, 745, 746, 1000, 1001, 1002}) {
        const auto it = itm::parse(read(*items.find(id)));
        // Options, Load and Exit put pressEnd one past their last frame.
        CHECK(it.animation && it.frameCount >= it.animation->pressEnd);
        const auto img = gim::decode(read(*images.find(it.image)));
        CHECK(img.height == it.frameCount * img.width);
    }

    // Every equipment item parses to its end and names an animation group of
    // its wearer's index (the item's directory says whose it is).
    int equipment = 0;
    for (const auto& [id, path] : items.entries) {
        const auto data = read(path);
        if (data.size() < 2 || data[0] < itm::HumanWeapon || data[0] > itm::RightGauntlet || data[1] != 0) continue;
        const auto it = itm::parse(data);
        CHECK(it.socketed);
        ++equipment;
    }
    CHECK(equipment > 100);
    std::printf("%d equipment items parsed\n", equipment);

    // The main cursor.
    const auto cursors = mgr::parse(read("res/common/managers/cursor.mgr"));
    const auto cursor = acr::parse(read(*cursors.find(1)));
    CHECK(cursor.hotX == 18 && cursor.hotY == 5 && cursor.frames.size() == 6);
    for (const auto& f : cursor.frames) CHECK(!gim::decode(read(f)).frames.empty());

    // The controls the front-end handlers address by index (FUN_004c9180 and
    // the rebuild's FrontEnd). res1's options.itf replaces res0's.
    const auto options = itf::parse(read("res/common/interface/start/options.itf"));
    CHECK(options.kind == 0x15 && options.controls.size() == 15);
    CHECK(options.controls[6].classId == itf::CheckBox && options.controls[14].classId == itf::CheckBox);
    for (size_t i : {8, 9, 10}) CHECK(options.controls[i].classId == itf::SliderBar && options.controls[i].images.size() == 6);
    CHECK(options.controls[7].classId == itf::TextBtn && options.controls[7].values[0] == 0x9000A);
    CHECK(options.controls[11].text == "Cancel" && int32_t(options.controls[11].values[1]) == -3);
    CHECK(options.controls[12].text == "Accept" && int32_t(options.controls[12].values[1]) == -2);
    // The slider track is 400 px with 25 px arrows: 350 px of travel, the
    // engine's hard-coded maximum position (0x15e).
    CHECK(gim::decode(read(options.controls[8].images[0])).width == 400);
    CHECK(gim::decode(read(options.controls[8].images[3])).width == 25);

    const auto load = itf::parse(read("res/common/interface/start/load.itf"));
    CHECK(load.kind == 0xd && load.controls.size() == 13);
    for (size_t i : {3, 8}) CHECK(load.controls[i].classId == itf::ScrollUp);
    for (size_t i : {4, 9}) CHECK(load.controls[i].classId == itf::ScrollDown);
    for (size_t i : {5, 10}) CHECK(load.controls[i].classId == itf::StaticTextBox);
    CHECK(load.controls[11].classId == itf::Icon && load.controls[11].values[0] == 860);

    const auto chooser = itf::parse(read("res/common/interface/start/new.itf"));
    CHECK(chooser.controls[1].classId == itf::Button && chooser.controls[1].name == "Play Button");
    CHECK(chooser.controls[6].classId == itf::StaticTextBox && chooser.controls[6].name == "Player Name");
}

int main() {
    testMgr();
    testItf();
    testFnt();
    testAcr();
    testItm();
    if (const char* dir = std::getenv("HARBINGER_DATA"))
        testRealData(dir);
    else
        std::printf("HARBINGER_DATA not set; skipping real-data checks\n");
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
