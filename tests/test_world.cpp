// Tests for the level (.chk), brush (.brs) and gizmo template (.giz) parsers
// and the level loader. Synthetic cases always run; with HARBINGER_DATA set,
// every shipped level, brush and gizmo must parse.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/brs.hpp"
#include "formats/chk.hpp"
#include "formats/giz.hpp"
#include "formats/mgr.hpp"
#include "world/level.hpp"

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

struct Bytes {
    std::vector<uint8_t> d;
    Bytes& u8(uint8_t v) { d.push_back(v); return *this; }
    Bytes& u16(uint16_t v) { return u8(uint8_t(v)).u8(uint8_t(v >> 8)); }
    Bytes& u32(uint32_t v) { return u16(uint16_t(v)).u16(uint16_t(v >> 16)); }
    Bytes& f64(double v) {
        uint64_t bits;
        std::memcpy(&bits, &v, sizeof v);
        return u32(uint32_t(bits)).u32(uint32_t(bits >> 32));
    }
    Bytes& str(const std::string& s) {
        u32(uint32_t(s.size()));
        d.insert(d.end(), s.begin(), s.end());
        return u8(0);
    }
    // .giz strings: u16 length counting the NUL.
    Bytes& gstr(const std::string& s) {
        u16(uint16_t(s.size() + 1));
        d.insert(d.end(), s.begin(), s.end());
        return u8(0);
    }
};

// A template header plus one mask and one frame.
static Bytes gizHeader(const char* name, uint16_t flags) {
    Bytes b;
    b.f64(1.04).gstr(name).u16(flags).u16(0x401).u8(0);
    b.u16(1).u16(1);                                          // one mask, one frame
    b.gstr("m.tga").u16(2).u16(2);                            // rows 2, two cells
    b.u16(0).u16(0).u16(0).u16(1).u16(0);
    b.u16(1).u16(0xFFFF).u16(0).u16(1).u16(0);
    b.u16(0).u16(5).u16(6).u16(100).u16(0).gstr("f.tga").u8(0);  // frame on mask 0 at (5, 6)
    b.u16(0xFFFF).u16(0xFFFF);                                 // version > 1.03
    return b;
}

static bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

static void testBrush() {
    Bytes b;
    b.str("Floor").u16(0).u16(0).u8(1);
    b.u16(2).u16(0x10).str("ca.tga").u16(0x24).str("cb.tga");  // centre: 16 tiles, then 4
    b.u16(1).u16(0x1001).str("n.tga");
    for (int g = 2; g < brs::GroupCount; ++g) b.u16(0);
    b.u16(1).u16(1).str("Chests");
    const brs::Brush brush = brs::parse(b.d);
    CHECK(brush.name == "Floor" && brush.walkable == 1);
    CHECK(brush.groups[brs::Centre].size() == 2 && brush.categories.size() == 1);
    CHECK(brs::find(brush, 0x10)->path == "ca.tga");
    CHECK(brs::find(brush, 0x1f)->path == "ca.tga");
    CHECK(brs::find(brush, 0x23)->path == "cb.tga");
    CHECK(brs::find(brush, 0x24) == nullptr);  // only 0x20..0x23
    CHECK(brs::find(brush, 0x1001)->path == "n.tga");
    CHECK(brs::find(brush, 0x1002) == nullptr);

    // Diamond positions inside the texture (FUN_004e2eb0's UV offsets).
    CHECK(brs::centreOffset(0x10).x == 0 && brs::centreOffset(0x10).y == 0);
    CHECK(brs::centreOffset(0x11).x == 128 && brs::centreOffset(0x11).y == 0);
    CHECK(brs::centreOffset(0x12).x == 64 && brs::centreOffset(0x12).y == 32);
    CHECK(brs::centreOffset(0x1f).x == 192 && brs::centreOffset(0x1f).y == 224);

    Bytes trailing = b;
    trailing.u8(0);
    CHECK(throws([&] { brs::parse(trailing.d); }));
}

static void testLevel() {
    Bytes c;
    c.f64(1.23).u16(1).u16(2).u16(1);  // version, act, 2x1 cells
    c.u16(1).u16(0).f64(1.5).f64(0.5).u16(4).str("Default").u32(0xFFFFFFFF);
    c.u16(0).u16(302).u16(0).str("Test Room").u16(0);
    c.u16(1).u16(1).u16(0x15);
    c.u16(2).u16(1).u16(0x16).u16(3).u16(0x4001);
    c.u16(2).u32(0).str("Broken").u16(1).u16(1).u16(0);  // variant 1 empties cell 1
    c.u16(0xAAAA);  // the undecoded rest
    const chk::Level lv = chk::parse(c.d);
    CHECK(lv.act == 1 && lv.width == 2 && lv.height == 1);
    CHECK(lv.entries.size() == 1 && lv.entries[0].name == "Default" && lv.entries[0].x == 1.5);
    CHECK(lv.entries[0].link == -1);
    CHECK(lv.name == "Test Room" && lv.music == 302);
    CHECK(lv.at(0, 0).count == 1 && lv.at(0, 0).layers[0].tile == 0x15);
    CHECK(lv.at(1, 0).count == 2 && lv.at(1, 0).layers[1].brush == 3);
    CHECK(lv.variants.size() == 1 && lv.variants[0].name == "Broken");
    CHECK(lv.variants[0].cells.size() == 1 && lv.variants[0].cells[0].first == 1);
    CHECK(lv.variants[0].cells[0].second.count == 0);
    CHECK(lv.cellsEnd == c.d.size() - 2);

    Bytes old;
    old.f64(1.2).u16(1).u16(1).u16(1);
    CHECK(throws([&] { chk::parse(old.d); }));

    // Projection: +x runs down-right, +y down-left, cells are 128x64.
    CHECK(world::toScreen(1, 0).x == 64 && world::toScreen(1, 0).y == 32);
    CHECK(world::toScreen(0, 1).x == -64 && world::toScreen(0, 1).y == 32);
}

static void testGizmo() {
    // A spawner with two slots and an empty script section.
    Bytes sp = gizHeader("Spawner", giz::HasScript | giz::Spawn);
    sp.u16(2).u16(50).u16(66).u16(7).u16(102).u16(40).u16(7);
    sp.u16(0).u16(0);
    const giz::Template t = giz::parse(sp.d);
    CHECK(t.name == "Spawner" && t.cls == giz::Spawn && t.version == 1.04);
    CHECK(t.masks.size() == 1 && t.masks[0].rows == 2 && t.masks[0].cells.size() == 2);
    CHECK(t.masks[0].cells[1].dy == -1);
    CHECK(t.frames.size() == 1 && t.frames[0].x == 5 && t.frames[0].y == 6 && t.frames[0].path == "f.tga");
    CHECK(t.spawns.size() == 2 && t.spawns[1].a == 102);
    CHECK(t.script.size() == 4);
    const uint8_t rec[12] = {};
    CHECK(t.recordExtra(rec, sizeof rec) == 12);  // 8 + a word per slot
    CHECK(throws([&] { t.recordExtra(rec, 11); }));

    // A chest: its label in the template; the record carries a flagged label too.
    Bytes ch = gizHeader("Locker", giz::Chest);
    ch.str("Floor Locker");
    const giz::Template chest = giz::parse(ch.d);
    CHECK(chest.label == "Floor Locker");
    Bytes r;
    r.u8(1).str("Floor Locker").u16(0x88).u16(0).u16(0).u16(0);
    CHECK(chest.recordExtra(r.d.data(), r.d.size()) == r.d.size());
    Bytes r0;
    r0.u8(0).u16(0).u16(0).u16(0).u16(0);
    CHECK(chest.recordExtra(r0.d.data(), r0.d.size()) == 9);

    Bytes trailing = ch;
    trailing.u8(0);
    CHECK(throws([&] { giz::parse(trailing.d); }));

    // Set gizmos after a level's cells.
    Bytes c;
    c.f64(1.23).u16(1).u16(2).u16(2).u16(0);
    c.u16(0).u16(302).u16(0).str("Room").u16(0);
    for (int i = 0; i < 4; ++i) c.u16(0);
    c.u16(1);                                     // the base variant only
    c.u16(1).u16(7);                              // its list keeps instance 7
    c.u32(2);
    c.u16(40).u16(7).u16(1).u16(0);               // the spawner at (1, 0)
    c.d.insert(c.d.end(), rec, rec + 12);
    c.u16(41).u16(9).u16(0).u16(1).d.insert(c.d.end(), r.d.begin(), r.d.end());
    c.u16(0xAAAA);
    chk::Level lv = chk::parse(c.d);
    chk::parseGizmos(lv, c.d, [&](uint16_t id) { return id == 40 ? &t : id == 41 ? &chest : nullptr; });
    CHECK(lv.instanceLists.size() == 1 && lv.instanceLists[0].size() == 1);
    CHECK(lv.keeps(lv.gizmos[0]) && !lv.keeps(lv.gizmos[1]));
    CHECK(lv.gizmos.size() == 2 && lv.gizmos[0].x == 1 && lv.gizmos[1].instance == 9);
    CHECK(lv.gizmos[1].extra.size() == r.d.size());
    CHECK(lv.gizmosEnd == c.d.size() - 2);
    CHECK(throws([&] { chk::parseGizmos(lv, c.d, [](uint16_t) { return nullptr; }); }));
}

static void testRealData(const char* dir) {
    Vfs vfs(dir);
    const auto ini = Ini::fromBytes(*vfs.read("Harbinger.ini"));

    size_t brushes = 0, levels = 0;
    for (const auto& p : vfs.list("res/"))
        if (endsWith(p, ".brs")) {
            ++brushes;
            try {
                const auto b = brs::parse(*vfs.read(p));
                for (const auto& group : b.groups)
                    for (const auto& t : group) CHECK(vfs.exists(t.path));
            } catch (const std::exception& e) {
                std::fprintf(stderr, "%s: %s\n", p.c_str(), e.what());
                ++failures;
            }
        }
    CHECK(brushes == 20);

    for (const auto& p : vfs.list("res/"))
        if (endsWith(p, ".chk")) {
            ++levels;
            try {
                const auto lv = chk::parse(*vfs.read(p));
                CHECK(lv.act >= 1 && lv.act <= 5);
                for (const auto& e : lv.entries) CHECK(e.x >= 0 && e.x < lv.width && e.y >= 0 && e.y < lv.height);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "%s: %s\n", p.c_str(), e.what());
                ++failures;
            }
        }
    CHECK(levels == 80);  // unique paths across res0 and res1

    // Every gizmo template parses to its last byte.
    size_t templates = 0;
    for (const auto& p : vfs.list("res/"))
        if (endsWith(p, ".giz")) {
            ++templates;
            try {
                giz::parse(*vfs.read(p));
            } catch (const std::exception& e) {
                std::fprintf(stderr, "%s: %s\n", p.c_str(), e.what());
                ++failures;
            }
        }
    CHECK(templates > 700);

    // Every level's set gizmos parse against its act's gizmo.mgr.
    std::map<std::pair<int, uint16_t>, giz::Template> loaded;
    size_t gizmos = 0;
    for (const auto& p : vfs.list("res/"))
        if (endsWith(p, ".chk")) {
            try {
                const auto data = *vfs.read(p);
                auto lv = chk::parse(data);
                const auto index = mgr::parse(*vfs.read(ini.require("Act" + std::to_string(lv.act), "Gizmos")));
                chk::parseGizmos(lv, data, [&](uint16_t id) -> const giz::Template* {
                    const auto key = std::make_pair(int(lv.act), id);
                    if (auto it = loaded.find(key); it != loaded.end()) return &it->second;
                    const std::string* path = index.find(id);
                    return path ? &loaded.emplace(key, giz::parse(*vfs.read(*path))).first->second : nullptr;
                });
                gizmos += lv.gizmos.size();
            } catch (const std::exception& e) {
                std::fprintf(stderr, "%s: %s\n", p.c_str(), e.what());
                ++failures;
            }
        }
    CHECK(gizmos > 10000);

    // A new game starts in level 1 (FUN_00497850 -> FUN_00497cd0(this, 1)).
    world::Level torvus(vfs, ini, 1);
    const auto& m = torvus.map();
    CHECK(m.name == "Torvus Junction" && m.act == 1 && m.width == 48 && m.height == 48);
    CHECK(m.music == 302);  // Res\\Common\\Music\\Haven.mp3 in sound.mgr
    CHECK(m.entries.size() == 2 && m.entries[0].name == "Default");
    size_t missing = 0;
    for (int x = 0; x < m.width; ++x)
        for (int y = 0; y < m.height; ++y)
            for (int l = 0; l < m.at(x, y).count; ++l)
                if (!torvus.image(m.at(x, y).layers[l])) ++missing;
    CHECK(missing == 0);
    CHECK(m.gizmos.size() == 51);
    for (const auto& g : m.gizmos)
        for (const auto& f : torvus.gizmoTemplate(g).frames) CHECK(torvus.gizmoImage(f) != nullptr);
}

int main() {
    testBrush();
    testLevel();
    testGizmo();
    if (const char* dir = std::getenv("HARBINGER_DATA"))
        testRealData(dir);
    else
        std::printf("HARBINGER_DATA not set; skipping real-data checks\n");
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
