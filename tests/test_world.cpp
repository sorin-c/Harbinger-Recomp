// Tests for the level (.chk), brush (.brs) and gizmo template (.giz) parsers
// and the level loader. Synthetic cases always run; with HARBINGER_DATA set,
// every shipped level, brush and gizmo must parse.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/agt.hpp"
#include "formats/anm.hpp"
#include "formats/brs.hpp"
#include "formats/chk.hpp"
#include "formats/gim.hpp"
#include "formats/gsc.hpp"
#include "formats/giz.hpp"
#include "formats/mgr.hpp"
#include "render/sprite.hpp"
#include "world/gizmo_state.hpp"
#include "world/level.hpp"
#include "world/script.hpp"
#include "world/loot.hpp"
#include "formats/gig.hpp"
#include "formats/itm.hpp"
#include "world/walkmap.hpp"

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
    {  // Frame timing (FUN_004305b0): a frame shows until its running total is reached.
        giz::Template t;
        for (uint16_t d : {0, 100, 100, 300}) {
            giz::Frame f;
            f.durationMs = d;
            t.frames.push_back(f);
        }
        t.frames[2].code = 1;
        CHECK(t.loopStart() == 2);
        CHECK(t.frameAt(0, 0, 3) == 0 && t.frameAt(1, 0, 3) == 1 && t.frameAt(100, 0, 3) == 1);
        CHECK(t.frameAt(101, 0, 3) == 2 && t.frameAt(250, 0, 3) == 3 && t.frameAt(501, 0, 3) == 1);
        CHECK(t.frameAt(450, 2, 3) == 2 && t.frameAt(0, 1, 1) == 1);
    }
    // A spawner with two slots and a one-triangle shape.
    Bytes sp = gizHeader("Spawner", giz::HasMesh | giz::Spawn);
    sp.u16(2).u16(50).u16(66).u16(7).u16(102).u16(40).u16(7);
    sp.u16(1).u16(0).u16(3);
    for (int i = 0; i < 3; ++i) sp.u16(i).u16(1).u16(0).u16(1).u16(0).u16(0).u16(4 * i).u16(8);
    sp.u16(1).u16(0).u16(1).u16(2).u16(0).u16(0).u16(0xffff);
    sp.u16(1);
    const giz::Template t = giz::parse(sp.d);
    CHECK(t.name == "Spawner" && t.cls == giz::Spawn && t.version == 1.04);
    CHECK(t.masks.size() == 1 && t.masks[0].rows == 2 && t.masks[0].cells.size() == 2);
    CHECK(t.masks[0].cells[1].dy == -1);
    CHECK(t.frames.size() == 1 && t.frames[0].x == 5 && t.frames[0].y == 6 && t.frames[0].path == "f.tga");
    CHECK(t.spawns.size() == 2 && t.spawns[1].a == 102);
    CHECK(t.mesh.size() == 1 && t.mesh[0].vertices.size() == 3 && t.mesh[0].triangles.size() == 1);
    CHECK(t.mesh[0].vertices[2].x == 2 && t.mesh[0].vertices[2].u == 8 && t.mesh[0].triangles[0].c == 2);
    CHECK(t.mesh[0].triangles[0].r == -1 && t.meshTail == 1);
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

// A template with one 100 ms frame per code, of class `cls`.
static giz::Template tagged(uint8_t cls, std::vector<uint16_t> codes) {
    giz::Template t;
    t.cls = cls;
    for (uint16_t c : codes) {
        giz::Frame f;
        f.durationMs = 100;
        f.code = c;
        t.frames.push_back(f);
    }
    return t;
}

static chk::GizmoRecord record(uint16_t instance, std::vector<uint8_t> extra) {
    chk::GizmoRecord g;
    g.instance = instance;
    g.extra = std::move(extra);
    return g;
}

static void testGizmoStates() {
    using world::GizmoPlay;
    // Doors and bridges: state 0 shows the first frame, any other the last.
    const giz::Template door = tagged(giz::Bridge, {3, 0, 0, 4});
    CHECK(world::startingPlay(door, record(1, {0, 0}), 0).first == 0);
    const GizmoPlay shut = world::startingPlay(door, record(1, {3, 0}), 0);
    CHECK(shut.mode == GizmoPlay::Hold && shut.first == 3);

    // Chests idle over the frames before BeginOpen; state 3 (opened) shows the last.
    const giz::Template chest = tagged(giz::Chest, {0, 0, 3, 0, 4});
    const GizmoPlay idle = world::startingPlay(chest, record(1, {0, 0, 0, 0, 0, 0, 0, 0, 0}), 0);
    CHECK(idle.mode == GizmoPlay::Loop && idle.first == 0 && idle.last == 1);
    CHECK(world::startingPlay(chest, record(1, {0, 0, 0, 0, 0, 0, 0, 3, 0}), 0).first == 4);

    // Consoles idle forth and back before BeginActivate, and go dark
    // without the power they need.
    const giz::Template console = tagged(giz::Console, {1, 0, 0, 2, 7, 8, 0});
    const std::vector<uint8_t> needsOne = {0, 0, 0, 0, 1, 0, 0, 0, 0};
    const GizmoPlay pp = world::startingPlay(console, record(1, needsOne), 1);
    CHECK(pp.mode == GizmoPlay::PingPong && pp.first == 0 && pp.last == 3);
    CHECK(world::startingPlay(console, record(1, needsOne), 0).first == 6);
    std::vector<size_t> seen;
    for (uint64_t ms = 50; ms < 800; ms += 100) seen.push_back(world::playFrame(console, pp, ms, 0));
    CHECK((seen == std::vector<size_t>{0, 1, 2, 3, 3, 2, 1, 0}));
    CHECK(world::playFrame(console, pp, 850, 0) == 0);

    // Breakers: on (3) loops from after EndOpen to before BeginClose.
    const giz::Template breaker = tagged(giz::Breaker, {0, 0, 4, 0, 0, 9, 0});
    const GizmoPlay on = world::startingPlay(breaker, record(1, {3, 0, 0, 0}), 0);
    CHECK(on.mode == GizmoPlay::Loop && on.first == 3 && on.last == 4);
    CHECK(world::startingPlay(breaker, record(1, {0, 0, 0, 0}), 0).mode == GizmoPlay::Hold);

    // Random pauses: rest on the frame before the first BeginIdle, sequences
    // from each BeginIdle; without power, frame 0.
    giz::Template light = tagged(giz::RandomPause, {0, 0, 1, 0, 1});
    light.classData = {10, 0, 0, 0, 20, 0, 0, 0};  // 100..200 ms
    const GizmoPlay p = world::startingPlay(light, record(5, {0, 0}), 0);
    CHECK(p.mode == GizmoPlay::Pauses && p.first == 1 && p.sequences.size() == 2);
    CHECK(p.sequences[0] == std::make_pair(size_t(2), size_t(3)) && p.sequences[1] == std::make_pair(size_t(4), size_t(4)));
    CHECK(p.minPauseMs == 10 && p.maxPauseMs == 20);
    CHECK(world::playFrame(light, p, 0, 5) == 1 && world::playFrame(light, p, 99, 5) == 1);
    std::set<size_t> shown;
    for (uint64_t ms = 0; ms < 20000; ms += 7) shown.insert(world::playFrame(light, p, ms, 5));
    CHECK((shown == std::set<size_t>{1, 2, 3, 4}));
    CHECK(world::startingPlay(light, record(5, {1, 0}), 0).mode == GizmoPlay::Hold);

    // Using: a console activates, toggles a door, and settles back.
    {
        world::GizmoState cs = world::startingState(console, record(1, needsOne), 0);
        CHECK(!world::activate(console, cs));  // no power
        cs.power = 1;
        CHECK(world::activate(console, cs) && cs.state == 2 && !world::activate(console, cs));
        CHECK(world::playFor(console, cs).first == 4 && world::playFor(console, cs).last == 5);
        CHECK(world::finish(console, cs) && cs.state == 0);

        world::GizmoState ds = world::startingState(door, record(2, {3, 0}), 0);
        CHECK(ds.state == 3 && world::toggle(door, ds, true) && ds.state == 1);  // opening ignores agents
        const GizmoPlay opening = world::playFor(door, ds);
        CHECK(opening.mode == GizmoPlay::Once && opening.reverse && world::playLength(door, opening) == 400);
        CHECK(world::playFrame(door, opening, 50, 0) == 3 && world::playFrame(door, opening, 350, 0) == 0);
        CHECK(world::playFrame(door, opening, 5000, 0) == 0);
        CHECK(!world::toggle(door, ds, false));  // already moving
        CHECK(world::finish(door, ds) && ds.state == 0);
        CHECK(!world::toggle(door, ds, true) && ds.state == 0);  // will not shut on an agent
        CHECK(world::toggle(door, ds, false) && ds.state == 2 && world::finish(door, ds) && ds.state == 3);

        world::GizmoState ch = world::startingState(chest, record(3, {0, 0, 0, 0, 0, 0, 0, 0, 0}), 0);
        CHECK(world::activate(chest, ch) && ch.state == 2);
        const GizmoPlay opens = world::playFor(chest, ch);
        CHECK(opens.mode == GizmoPlay::Once && opens.first == 2 && opens.last == 4);
        CHECK(world::finish(chest, ch) && ch.state == 3 && !world::activate(chest, ch));
    }

    // Power: a source feeds a breaker, which passes it on only while on.
    chk::Level lv;
    lv.gizmos = {record(1, {3, 0, 0, 0}), record(2, {0, 0, 0, 0}), record(3, needsOne), record(4, needsOne)};
    lv.powerSources = {{1, 1}, {2, 1}};
    lv.powers = {{1, {3}}, {2, {4}}};
    const auto power = world::startingPower(lv, [&](const chk::GizmoRecord& g) -> const giz::Template& {
        return g.instance <= 2 ? breaker : console;
    });
    CHECK(power.at(1) == 1 && power.at(3) == 1 && power.count(4) == 0);
}

static void testAgents() {
    // FUN_004e12d0's tables, at their edges.
    CHECK(anm::directionFor(0, 16) == 0 && anm::directionFor(8, 16) == 0 && anm::directionFor(9, 16) == 1);
    CHECK(anm::directionFor(246, 16) == 15 && anm::directionFor(247, 16) == 0);
    CHECK(anm::directionFor(14, 10) == 0 && anm::directionFor(15, 10) == 1 && anm::directionFor(241, 10) == 9);
    CHECK(anm::directionFor(242, 10) == 0 && anm::directionFor(100, 8) == 0);
    {  // Marker entries 0-5 of an agent sheet, in both channel orders seen in the files.
        gim::Image img;
        img.palette = {{0, 255, 0, 255}, {255, 0, 0, 255}, {0, 171, 0, 255}, {0, 85, 0, 255},
                       {171, 85, 0, 255}, {85, 171, 0, 255}, {9, 9, 9, 255}};
        CHECK(render::agentMarkerAlpha(img, 0) == 0 && render::agentMarkerAlpha(img, 1) == 255);
        CHECK(render::agentMarkerAlpha(img, 2) == 0 && render::agentMarkerAlpha(img, 3) == 0);
        CHECK(render::agentMarkerAlpha(img, 4) == 171 && render::agentMarkerAlpha(img, 5) == 85);
        CHECK(render::agentMarkerAlpha(img, 6) == 0);
        img.palette = {{255, 0, 0, 255}, {0, 255, 0, 255}, {171, 0, 0, 255}, {85, 0, 0, 255},
                       {0, 255, 0, 255}, {0, 255, 0, 255}, {9, 9, 9, 255}};
        CHECK(render::agentMarkerAlpha(img, 1) == 255 && render::agentMarkerAlpha(img, 2) == 0);
        CHECK(render::agentMarkerAlpha(img, 4) == 255);
    }
    // .anm: header 0x0b = 10 directions with a hotspot; direction 0 has two frames.
    Bytes a;
    a.u16(0x0b).u16(22).u16(44).u32(0);
    a.u32(2).u32(1).str("Res\\A\\Idle\\0\\00.tga").str("Res\\A\\Idle\\0\\01.tga");
    for (int i = 0; i < 6; ++i) a.u8(0);
    for (int d = 1; d < 10; ++d) a.u32(0).u32(0);
    a.u32(0);
    const anm::Animation anim = anm::parse(a.d);
    CHECK(anim.directions.size() == 10 && anim.hotspot && anim.hotspot->second == 44);
    CHECK(anim.directions[0].frames.size() == 2 && anim.directions[0].actions.size() == 1);
    CHECK(anim.firstDirection() == &anim.directions[0]);
    CHECK(anm::sheetPath("Res\\A\\Idle\\0\\00.tga") == "Res\\A\\Idle\\fsp\\Idle0FSP.gim");
    Bytes plain;  // header 0: 16 directions, no hotspot
    plain.u16(0).u32(0);
    for (int d = 0; d < 16; ++d) plain.u32(0).u32(0);
    plain.u32(0);
    CHECK(anm::parse(plain.d).directions.size() == 16 && !anm::parse(plain.d).hotspot);

    // .agt: an NPC (class 24) with one animation.
    Bytes t;
    t.u16(24).u16(24).f64(1.96).u16(100).f64(0).f64(100);
    for (int i = 0; i < 10; ++i) t.u16(uint16_t(i));
    t.str("Torvus").u8(1).u16(0).u16(19).u16(0);
    t.u32(1).u16(0).u16(0).str("Res\\A\\Idle\\Idle.anm");
    for (int i = 0; i < 3; ++i) t.u16(12);
    t.u16(0).u16(0);
    t.u16(1).u32(2).u16(393).u16(394);  // one list of two words
    t.u8(1).u16(0).u16(100);
    for (int i = 0; i < 11; ++i) t.u8(0);
    const agt::Template npc = agt::parse(t.d);
    CHECK(npc.name == "Torvus" && npc.isNpc() && npc.stats.size() == 10);
    CHECK(npc.anims.size() == 1 && npc.anims[0].path == "Res\\A\\Idle\\Idle.anm");
    CHECK(npc.recordHasExtraWord());
    Bytes trailing = t;
    trailing.u8(0);
    CHECK(throws([&] { agt::parse(trailing.d); }));

    // Agents after a level's gizmos.
    Bytes c;
    c.f64(1.23).u16(1).u16(2).u16(2).u16(0);
    c.u16(0).u16(302).u16(0).str("Room").u16(0);
    for (int i = 0; i < 4; ++i) c.u16(0);
    c.u16(1).u16(0).u32(0);                       // one variant, no gizmos
    c.u16(1).u16(3);                              // agent list: instance 3
    c.u32(1).u16(51).u16(3).u8(64).u8(0).u16(1).u16(0).f64(1.5).f64(0.5);
    c.u8(1).str("Torvus").u8(1).u16(0).u16(0).u16(24);
    c.u16(0xAAAA);
    chk::Level lv = chk::parse(c.d);
    chk::parseGizmos(lv, c.d, [](uint16_t) { return nullptr; });
    chk::parseAgents(lv, c.d, [&](uint16_t id) { return id == 51 ? &npc : nullptr; });
    CHECK(lv.agents.size() == 1 && lv.agents[0].facing == 64 && lv.agents[0].x == 1.5);
    CHECK(lv.agents[0].label == "Torvus" && lv.agents[0].extra == 24 && lv.keeps(lv.agents[0]));
    CHECK(lv.agentsEnd == c.d.size() - 2);
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
    std::map<uint16_t, agt::Template> agentTemplates;
    const auto agentIndex = mgr::parse(*vfs.read(ini.require("Paths", "Agents")));
    size_t gizmos = 0, agents = 0, triggers = 0, scripted = 0;
    std::set<uint16_t> scriptIds;
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
                chk::parseAgents(lv, data, [&](uint16_t id) -> const agt::Template* {
                    if (auto it = agentTemplates.find(id); it != agentTemplates.end()) return &it->second;
                    const std::string* path = agentIndex.find(id);
                    return path ? &agentTemplates.emplace(id, agt::parse(*vfs.read(*path))).first->second : nullptr;
                });
                agents += lv.agents.size();
                chk::parseRest(lv, data);  // to the last byte
                CHECK(lv.beacons.size() == lv.rooms.size());
                triggers += lv.eventTriggers.size() + lv.gizmoTriggers.size();
                for (const auto& t : lv.eventTriggers)
                    if (t.script) ++scripted, scriptIds.insert(t.script);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "%s: %s\n", p.c_str(), e.what());
                ++failures;
            }
        }
    CHECK(gizmos > 10000 && agents > 5000);
    CHECK(triggers > 500 && scripted > 30);

    // Every cutscene script parses, and every script an event trigger names
    // exists in each character's script manager.
    size_t scripts = 0;
    for (const auto& p : vfs.list("res/"))
        if (endsWith(p, ".gsc")) {
            ++scripts;
            try {
                const auto d = *vfs.read(p);
                gsc::parse(std::string_view(reinterpret_cast<const char*>(d.data()), d.size()));
            } catch (const std::exception& e) {
                std::fprintf(stderr, "%s: %s\n", p.c_str(), e.what());
                ++failures;
            }
        }
    CHECK(scripts > 200);
    // ThoughtBubble's last number is a voice line in the same character's
    // speech manager.
    size_t voiced = 0, unvoiced = 0;
    for (const char* who : {"Human", "Gladiator", "Culibine"}) {
        const auto idx = mgr::parse(*vfs.read(ini.require(who, "Scripts")));
        const auto speech = mgr::parse(*vfs.read(ini.require(who, "Speech")));
        for (uint16_t id : scriptIds) {
            const std::string* path = idx.find(id);
            CHECK(path && vfs.exists(*path));
            if (!path || !vfs.exists(*path)) continue;
            const auto d = *vfs.read(*path);
            for (const auto& l : gsc::parse(std::string_view(reinterpret_cast<const char*>(d.data()), d.size())).lines) {
                if (l.command != gsc::Command::ThoughtBubble || l.args.size() < 6) continue;
                const std::string* voice = speech.find(uint16_t(l.number(5)));
                if (voice && vfs.exists(*voice)) ++voiced;
                else ++unvoiced;
            }
        }
    }
    CHECK(voiced > 50 && unvoiced == 0);

    // Every item group parses, and every chest's group and item resolve.
    {
        size_t groups = 0, chests = 0, withGroup = 0;
        const auto items = mgr::parse(*vfs.read(ini.require("Paths", "Items")));
        size_t headers = 0;
        for (const auto& [id, path] : items.entries)
            if (const auto d = vfs.read(path)) {
                itm::parseHeader(*d);
                ++headers;
            }
        CHECK(headers > 1000);
        for (const char* who : {"Human", "Gladiator", "Culibine"}) {
            const auto idx = mgr::parse(*vfs.read(ini.require(who, "ItemGroups")));
            for (const auto& [id, path] : idx.entries) {
                const auto d = vfs.read(path);
                CHECK(d);
                if (!d) continue;
                const gig::Group g = gig::parse(*d);
                // (Culibine basegladblade3 has money 10..2; it holds no money item.)
                CHECK(g.countMin <= g.countMax);
                for (const auto& e : g.entries) CHECK(items.find(e.item));
                ++groups;
            }
        }
        CHECK(groups > 300);
        const auto human = mgr::parse(*vfs.read(ini.require("Human", "ItemGroups")));
        for (const auto& [id, path] : mgr::parse(*vfs.read(ini.require("Paths", "Levels"))).entries) {
            world::Level lv(vfs, ini, id);
            for (const auto& gz : lv.map().gizmos) {
                const auto c = world::chestContents(lv.gizmoTemplate(gz), gz);
                if (!c) continue;
                ++chests;
                if (c->group) {
                    ++withGroup;
                    CHECK(human.find(c->group));
                }
                if (c->item) CHECK(items.find(c->item));
            }
        }
        CHECK(chests > 1000 && withGroup > 900);
    }

    // Refuse Disposal: walking up to the portal sets off trigger 4, whose
    // script (id 11) is the portal scene with seven thought bubbles.
    {
        world::Level trash(vfs, ini, 11);
        world::TriggerWatch watch(trash.map());
        const auto fired = watch.step(68.48, 54.58, 62.0, 57.0);
        CHECK(fired.size() == 1 && fired[0]->id == 4 && fired[0]->script == 11);
    }

    // A sheet holds one frame per frame of its direction (all but a few
    // editor leftovers).
    size_t agentFiles = 0, anims = 0, sheets = 0, sheetMismatch = 0;
    for (const auto& p : vfs.list("res/"))
        try {
            if (endsWith(p, ".agt")) {
                ++agentFiles;
                agt::parse(*vfs.read(p));
            } else if (endsWith(p, ".anm")) {
                ++anims;
                for (const auto& d : anm::parse(*vfs.read(p)).directions)
                    if (!d.frames.empty()) {
                        ++sheets;
                        const bool direct = vfs.exists(d.frames[0].substr(0, d.frames[0].size() - 4) + ".gim");
                        CHECK(vfs.exists(anm::sheetPath(d.frames[0])) || direct);
                        if (const auto sd = vfs.read(anm::sheetPath(d.frames[0])))
                            if (gim::decode(*sd).frames.size() != d.frames.size()) ++sheetMismatch;
                    }
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s: %s\n", p.c_str(), e.what());
            ++failures;
        }
    CHECK(agentFiles >= 112 && anims == 550 && sheets > 5000 && sheetMismatch <= 11);

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
    CHECK(m.agents.size() == 2 && torvus.agentTemplate(m.agents[0]).name == "Torvus");
    for (const auto& a : m.agents) CHECK(torvus.agentPose(a).image != nullptr);
    // The town generator (Looped) turns: its frame changes over time.
    size_t looping = 0;
    for (const auto& g : m.gizmos)
        if (torvus.gizmoFrame(g, 150) != torvus.gizmoFrame(g, 450)) ++looping;
    CHECK(looping > 0);
    for (const auto& g : m.gizmos)
        for (const auto& f : torvus.gizmoTemplate(g).frames) CHECK(torvus.gizmoImage(f) != nullptr);

    // Starting states. Vantir Storage: the door by the entrance is shut (its
    // last frame, a whole picture) and its console idles.
    world::Level storage(vfs, ini, 3);
    for (const auto& g : storage.map().gizmos) {
        if (g.instance == 151) {
            CHECK(storage.gizmoTemplate(g).name == "Door SE (mech)" && storage.gizmoFrame(g, 0) == 15);
            CHECK((storage.gizmoLayers(g, 15) == std::vector<size_t>{15}));
        }
        if (g.instance == 149) CHECK(storage.gizmoPlay(g).mode == world::GizmoPlay::PingPong);
    }
    // Their 3D shapes: the door's shape covers every pixel of its moving
    // frames and none of its first two (an editor outline of the open
    // door). The shut door stands in front of its console, on the wall
    // inside the doorway.
    {
        const chk::GizmoRecord* console = storage.gizmoByInstance(149);
        const chk::GizmoRecord* door = storage.gizmoByInstance(151);
        CHECK(console && door);
        if (console && door) {
            const auto& frames = storage.gizmoTemplate(*door).frames;
            for (size_t i = 0; i < frames.size(); ++i) {
                const render::RgbaImage* raw = storage.gizmoImage(frames[i]);
                const render::RgbaImage* shown = storage.gizmoFrameImage(*door, i);
                size_t opaque = 0, kept = 0;
                for (size_t k = 0; k < raw->pixels.size(); ++k)
                    opaque += raw->pixels[k] >> 24 != 0, kept += shown->pixels[k] >> 24 != 0;
                CHECK(opaque > 0);
                CHECK(kept == (i < 2 ? 0 : opaque));
            }
            CHECK(storage.gizmoInFront(*door, *console) == 1 && storage.gizmoInFront(*console, *door) == -1);
        }
    }
    // Using that console opens the door: it plays its frames backwards to
    // frame 0 (open), the console returns to idle, and the walk grid lets
    // the player through the doorway.
    {
        const chk::GizmoRecord* console = storage.gizmoByInstance(149);
        const chk::GizmoRecord* door = storage.gizmoByInstance(151);
        CHECK(console && door);
        if (console && door) {
            world::WalkMap before(storage);
            const auto use = storage.gizmoUsePoint(*console);
            CHECK(std::abs(use.x - (console->x + 0.5)) < 3 && std::abs(use.y - (console->y + 0.5)) < 3);
            storage.update(1000);
            CHECK(storage.useGizmo(*console, 1000, [](const chk::GizmoRecord&) { return false; }));
            CHECK(storage.gizmoState(*console).state == 2 && storage.gizmoState(*door).state == 1);
            CHECK(!storage.useGizmo(*console, 1100, [](const chk::GizmoRecord&) { return false; }));  // busy
            CHECK(storage.gizmoFrame(*door, 1050) == 15);
            bool masksChanged = false;
            world::WalkMap patched(storage);
            for (uint64_t ms = 1000; ms <= 4000; ms += 50)
                for (const chk::GizmoRecord* g : storage.update(ms)) {
                    masksChanged = true;
                    patched.repaint(storage, *g);
                }
            CHECK(masksChanged);
            CHECK(storage.gizmoState(*door).state == 0 && storage.gizmoFrame(*door, 4000) == 0);
            CHECK(storage.gizmoState(*console).state == 0);
            world::WalkMap after(storage);
            size_t opened = 0;
            for (const auto& c : storage.gizmoTemplate(*door).masks[0].cells) {
                const int sx = 2 * door->x + c.dx, sy = 2 * door->y + 1 + c.dy;
                if (before.inside(sx, sy) && before.at(sx, sy).clearance == world::WalkMap::kBlocked &&
                    after.at(sx, sy).clearance != world::WalkMap::kBlocked)
                    ++opened;
            }
            CHECK(opened > 0);
            // Repainting only the door's area gives the same grid as a fresh build.
            size_t differ = 0;
            for (int sx = 0; sx < after.width(); ++sx)
                for (int sy = 0; sy < after.height(); ++sy)
                    differ += after.at(sx, sy).clearance != patched.at(sx, sy).clearance ||
                              after.at(sx, sy).height != patched.at(sx, sy).height;
            CHECK(differ == 0);
            // And again: it shuts, unless someone stands in the doorway.
            CHECK(storage.useGizmo(*console, 5000, [](const chk::GizmoRecord&) { return true; }));
            CHECK(storage.gizmoState(*door).state == 0);
            for (uint64_t ms = 5000; ms <= 8000; ms += 50) storage.update(ms);
            CHECK(storage.useGizmo(*console, 9000, [](const chk::GizmoRecord&) { return false; }));
            CHECK(storage.gizmoState(*door).state == 2);
        }
    }
    // Across the game: most doors start shut, some consoles have no power,
    // and the lights and incubators that have it pause and play.
    size_t shut = 0, open = 0, dark = 0, pausing = 0, opaquePx = 0, coveredPx = 0;
    std::set<std::string> covered;
    for (const auto& [id, path] : mgr::parse(*vfs.read(ini.require("Paths", "Levels"))).entries) {
        world::Level lv(vfs, ini, id);
        for (const auto& g : lv.map().gizmos) {
            const giz::Template& t = lv.gizmoTemplate(g);
            const world::GizmoPlay& p = lv.gizmoPlay(g);
            CHECK(p.first <= p.last && p.last < t.frames.size());
            for (uint64_t ms : {0, 1234, 98765}) CHECK(lv.gizmoFrame(g, ms) < t.frames.size());
            if (t.cls == giz::Bridge) ++(p.first == 0 ? open : shut);
            if (t.cls == giz::Console && p.mode == world::GizmoPlay::Hold && p.first + 1 == t.frames.size()) ++dark;
            if (p.mode == world::GizmoPlay::Pauses) ++pausing;
            // The shapes cover the pictures (what they leave out is mostly
            // editor art: outlines, fill markers).
            if (!t.frames.empty() && covered.insert(t.name).second) {
                const render::RgbaImage* img = lv.gizmoImage(t.frames[0]);
                const auto& shape = lv.gizmoShape(g);
                for (int y = 0; img && y < img->height; ++y)
                    for (int x = 0; x < img->width; ++x)
                        if (img->pixels[size_t(y) * size_t(img->width) + size_t(x)] >> 24)
                            ++opaquePx, coveredPx += !std::isnan(shape.at(x, y));
            }
        }
    }
    CHECK(shut > 200 && open > 10 && dark > 0 && pausing > 300);
    CHECK(opaquePx > 0 && coveredPx > opaquePx * 95 / 100);
}

static void testScript() {
    const auto s = gsc::parse(
        "Version 0.1\r\n"
        "Cinematic(On, Visible)\r\n"
        "ThoughtBubble(\"So, this\\nis it.\", 265, 195, 0, 0, 50134)\r\n"
        "PositionCamera(23.0, 16.0, 9.0, Wait)\r\n"
        "\r\n"
        "QuitGame()\r\n");
    CHECK(s.version == 0.1 && s.lines.size() == 4);
    CHECK(s.lines[1].command == gsc::Command::ThoughtBubble && s.lines[1].text(0) == "So, this\nis it.");
    CHECK(s.lines[1].number(5) == 50134 && !s.lines[1].waits());
    CHECK(s.lines[2].number(0) == 23.0 && s.lines[2].waits() && s.lines[2].lineNumber == 4);
    CHECK(s.lines[3].command == gsc::Command::QuitGame && s.lines[3].args.empty());
    bool threw = false;
    try {
        gsc::parse("Version 0.0\r\nThoughtBubble(\"x\", 1, 2, 3, 4, 5)\r\n");  // no voice id before 0.1
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

static void testScriptRunning() {
    // FUN_0048b390: a move that starts inside, crosses, or only nears the circle.
    CHECK(world::entersCircle(0.5, 0, 0.5, 0, 0, 0, 1));    // standing inside
    CHECK(world::entersCircle(-3, 0, 3, 0, 0, 0, 1));       // straight through
    CHECK(world::entersCircle(-3, 0, -0.9, 0, 0, 0, 1));    // stops just inside
    CHECK(!world::entersCircle(-3, 0, -1.1, 0, 0, 0, 1));   // stops short
    CHECK(!world::entersCircle(-3, 1.5, 3, 1.5, 0, 0, 1));  // passes by
    CHECK(!world::entersCircle(2, 0, 2, 0, 0, 0, 1));       // standing outside

    chk::Level level;
    chk::EventTrigger t;
    t.x = 10, t.y = 10, t.radius = 2, t.id = 7;
    level.eventTriggers.push_back(t);
    world::TriggerWatch watch(level);
    CHECK(watch.step(0, 0, 1, 1).empty());
    const auto fired = watch.step(5, 5, 9, 9);
    CHECK(fired.size() == 1 && fired[0]->id == 7);
    CHECK(watch.step(9, 9, 10, 10).empty());  // once only

    // FUN_004fd8a0: each command runs until it says it is done; done or
    // skipped commands hand on to the next one in the same frame.
    auto script = std::make_shared<gsc::Script>(gsc::parse(
        "Version 0.1\r\nWait(100)\r\nSound(5, NoWait)\r\nWait(50)\r\nShake(1, 1, 10, Rndm, Rndm, Wait)\r\n"));
    world::ScriptRunner runner;
    std::vector<std::pair<int, bool>> calls;  // (line, first call)
    uint64_t until = 0;
    auto host = [&](const gsc::Line& l, bool first, uint64_t now) {
        calls.emplace_back(l.lineNumber, first);
        if (l.command == gsc::Command::Sound) return world::CommandResult::Skip;
        if (first) until = now + uint64_t(l.number(l.command == gsc::Command::Wait ? 0 : 2));
        return now >= until ? world::CommandResult::Done : world::CommandResult::Running;
    };
    runner.start(script);
    CHECK(!runner.update(host, 0) && runner.line() == 0);
    CHECK(!runner.update(host, 50) && runner.line() == 0);
    CHECK(!runner.update(host, 100) && runner.line() == 2);  // Wait done, Sound skipped, Wait(50) started
    CHECK(!runner.update(host, 150) && runner.line() == 3);
    CHECK(runner.update(host, 160) && !runner.running());
    const std::vector<std::pair<int, bool>> expect{{2, true},  {2, false}, {2, false}, {3, true},
                                                   {4, true},  {4, false}, {5, true},  {5, false}};
    CHECK(calls == expect);
}

static void testLoot() {
    // A group: money 5..15, one or two drops, item 12 (weight 1) or 7 (weight 3).
    const std::vector<uint8_t> bytes{0, 0, 0, 0, 0, 0, 0, 0, 5, 15, 1, 2, 2, 0, 0, 0, 12, 0, 1, 0, 7, 0, 3, 0};
    const gig::Group g = gig::parse(bytes);
    CHECK(g.moneyMin == 5 && g.moneyMax == 15 && g.countMin == 1 && g.countMax == 2);
    CHECK(g.entries.size() == 2 && g.entries[1].item == 7 && g.totalWeight() == 4);
    bool threw = false;
    try {
        gig::parse(std::vector<uint8_t>(bytes.begin(), bytes.end() - 1));
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);

    auto isMoney = [](uint16_t id) { return id == 12; };
    world::Rng rng(1234);
    std::map<uint16_t, int> seen;
    for (int i = 0; i < 2000; ++i) {
        const auto drops = world::rollDrops(&g, 0, isMoney, rng);
        CHECK(drops.size() >= 1 && drops.size() <= 2);
        for (const auto& d : drops) {
            ++seen[d.item];
            if (d.item == 12) CHECK(d.amount >= 5 && d.amount < 15);
            else CHECK(d.amount == 0);
        }
    }
    // FUN_00441dc0 picks the first entry whose running total reaches
    // rand % total: item 12 for 0 and 1, item 7 for 2 and 3.
    CHECK(seen[12] > seen[7] / 2 && seen[12] < seen[7] * 2);
    // The chest's own item drops unless the group already gave it.
    const auto withOwn = world::rollDrops(nullptr, 62, isMoney, rng);
    CHECK(withOwn.size() == 1 && withOwn[0].item == 62);
    CHECK(world::rollDrops(nullptr, 0, isMoney, rng).empty());
    gig::Group only7 = g;
    only7.entries = {{7, 1}};
    only7.countMin = only7.countMax = 1;
    const auto once = world::rollDrops(&only7, 7, isMoney, rng);
    CHECK(once.size() == 1 && once[0].item == 7);
}

int main() {
    testBrush();
    testLevel();
    testGizmo();
    testGizmoStates();
    testAgents();
    testScript();
    testScriptRunning();
    testLoot();
    if (const char* dir = std::getenv("HARBINGER_DATA"))
        testRealData(dir);
    else
        std::printf("HARBINGER_DATA not set; skipping real-data checks\n");
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
