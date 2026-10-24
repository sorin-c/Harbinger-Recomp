// Tests for walking: the facing arithmetic, the walk grid and path finder,
// the animation-group index and the player. Synthetic cases always run; with
// HARBINGER_DATA set, every character, animation group and level is checked.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/animgroup.hpp"
#include "formats/mgr.hpp"
#include "world/level.hpp"
#include "world/motion.hpp"
#include "world/player.hpp"
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

static void testFacing() {
    using world::facingTowards;
    CHECK(facingTowards(1, 0) == 0);
    CHECK(facingTowards(0, 1) == 64);
    CHECK(facingTowards(-1, 0) == 128);
    CHECK(facingTowards(0, -1) == 192);
    CHECK(facingTowards(1, 1) == 32);
    CHECK(facingTowards(-1, -1) == 160);
    CHECK(facingTowards(0, 0) == 0);
    // atan(1/64) is 0.9 degrees: rounded up by the +0.5, then 1.39 * 0.711 truncates to 0.
    CHECK(facingTowards(64, 1) == 0);
    // atan(32/64) = 26.57 degrees -> (27.07) * 0.7111 = 19.25 -> 19.
    CHECK(facingTowards(2, 1) == 19);
    CHECK(facingTowards(1, 2) == 64 - 19);
    CHECK(std::fabs(world::facingCos(0) - 1) < 1e-12 && std::fabs(world::facingSin(64) - 1) < 1e-12);
    CHECK(std::fabs(world::facingCos(128) + 1) < 1e-12 && std::fabs(world::facingSin(0)) < 1e-12);
    // The Human's move parameter, 288: 3.18 cells a second (measured under Wine: 3.18).
    CHECK(std::fabs(world::walkSpeed(288) * 1000 - 3.182) < 0.001);
}

static void testWalkMap() {
    // 12x12 half cells, open, with a wall (height 6) across x = 6 for y < 9.
    world::WalkMap w(12, 12);
    for (int y = 0; y < 9; ++y) {
        w.at(6, y).clearance = world::WalkMap::kBlocked;
        w.at(6, y).height = 6;
    }
    w.computeClearance();
    CHECK(w.at(6, 0).clearance == world::WalkMap::kBlocked);
    // The ring of 2x2 whose lower-right is the half cell: (7, y) sees x = 6.
    CHECK(w.at(7, 3).clearance == 2);
    CHECK(w.at(5, 3).clearance == 1);  // x = 6 is only in the 4x4 ring
    CHECK(w.at(4, 3).clearance == 0);  // only in the 6x6 ring, which counts as clear
    CHECK(w.at(2, 3).clearance == 0);
    CHECK(w.at(8, 3).clearance == 1);
    CHECK(w.at(6, 11).clearance == 0);
    const uint8_t player = world::WalkMap::limitFor(1);
    CHECK(player == 2);
    CHECK(w.passable(5, 3, player) && !w.passable(7, 3, player) && w.passable(7, 3, world::WalkMap::limitFor(0)));

    // A straight walk needs no waypoints but the target.
    auto p = w.findPath({1.25, 1.25}, {2.75, 4.75}, player);
    CHECK(p.size() == 1 && p[0].x == 2.75 && p[0].y == 4.75);
    // Across the wall: around its open end, ending on the target.
    p = w.findPath({1.25, 1.25}, {5.25, 1.25}, player);
    CHECK(p.size() >= 2);
    if (!p.empty()) CHECK(p.back().x == 5.25 && p.back().y == 1.25);
    for (const auto& q : p) CHECK(w.passableAt(q.x, q.y, player));
    bool rounds = false;
    for (const auto& q : p) rounds |= q.y >= 4.5;
    CHECK(rounds);
    // A blocked target: walk the straight line as far as it goes.
    p = w.findPath({1.25, 1.25}, {3.25, 1.25}, player);
    CHECK(p.size() == 1 && p[0].x < 3.0);
    // Off the map: nowhere to go.
    CHECK(w.findPath({1.25, 1.25}, {-3, 1}, player).empty());
}

static void testAnimGroup() {
    // Two groups, 3 states.
    std::vector<uint8_t> d = {2, 0, 0, 0, 1, 0, 2, 0};
    auto str = [&](const std::string& s, bool nulCounted) {
        const uint32_t n = uint32_t(s.size() + (nulCounted ? 1 : 0));
        for (int i = 0; i < 4; ++i) d.push_back(uint8_t(n >> (8 * i)));
        d.insert(d.end(), s.begin(), s.end());
        d.push_back(0);
    };
    str("Armor 7", true);
    str("Rifle 2", true);
    str("Human", false);
    for (uint16_t v : {3, 3, 5, 9, 0, 0, 7}) {  // 3 states, then 2 x 3 animation ids
        d.push_back(uint8_t(v));
        d.push_back(uint8_t(v >> 8));
    }
    const auto idx = animgroup::parse(d);
    CHECK(idx.name == "Human" && idx.groups.size() == 2);
    CHECK(idx.find("Rifle 2") && idx.find("Rifle 2")->id == 2);
    CHECK(idx.find("Armor 7")->byState.size() == 3 && idx.find("Armor 7")->byState[1] == 5);
    CHECK(idx.find("Rifle 2")->byState[2] == 7);
    auto bad = d;
    bad.pop_back();
    CHECK(throws([&] { animgroup::parse(bad); }));
}

static void testRealData(const char* dir) {
    Vfs vfs(dir);
    const Ini ini = Ini::fromBytes(*vfs.read("Harbinger.ini"));
    // Every character's animation groups parse, and name animations its index has.
    for (const char* section : {"Human", "Gladiator", "Culibine"}) {
        const auto groups = animgroup::parse(*vfs.read(ini.require(section, "AnimGroups")));
        const auto anims = mgr::parse(*vfs.read(ini.require(section, "Anims")));
        CHECK(groups.name == section);
        for (const auto& g : groups.groups)
            for (uint16_t a : g.byState)
                if (a) CHECK(anims.find(a) && vfs.exists(*anims.find(a)));
    }

    // Level 1: the player starts on open floor and its first click in the
    // Wine run (view pixel 300, 250 -> map 16.3, 24.5) is a straight walk.
    world::Level level(vfs, ini, 1);
    world::WalkMap walk(level);
    const auto& start = level.map().entries[0];
    for (auto c : {world::Character::Human, world::Character::Gladiator, world::Character::Culibine}) {
        world::Player p(vfs, ini, c, walk, start);
        CHECK(p.limit() == 2 && p.speed() > 0);
        CHECK(walk.passableAt(p.x(), p.y(), p.limit()));
        uint64_t now = 0;
        for (int f = 0; f < 256; f += 16) {
            p.steer(world::facingCos(uint8_t(f)), world::facingSin(uint8_t(f)));
            p.update(now += 10);
            CHECK(p.state() == agt::Move && p.facing() == f && p.pose(now).image);
            p.steer(0, 0);
            p.update(now += 10);
            CHECK(p.state() == agt::Idle && p.pose(now).image);
        }
    }
    world::Player human(vfs, ini, world::Character::Human, walk, start);
    // The starting items' animation groups, by layer (decoded, and the
    // Human's match the original's sprite).
    CHECK((human.layers() == std::vector<std::string>{"Armor 7", "Rifle 2"}));
    CHECK((world::Player(vfs, ini, world::Character::Gladiator, walk, start).layers() ==
           std::vector<std::string>{"Base Torso", "Base Blade", "Base Gun"}));
    CHECK(world::Player(vfs, ini, world::Character::Culibine, walk, start).layers().empty());
    CHECK(human.walkTo(16.3, 24.5) && human.path().size() == 1);
    uint64_t t = 0;
    for (; t < 5000 && !human.path().empty(); t += 10) human.update(t);
    CHECK(human.path().empty() && std::fabs(human.x() - 16.3) < 1e-9 && std::fabs(human.y() - 24.5) < 1e-9);
    // 3.95 cells at 3.18 cells a second.
    CHECK(t > 1150 && t < 1350);

    // Every level builds a walk grid with somewhere to stand.
    const auto levels = mgr::parse(*vfs.read(ini.require("Paths", "Levels")));
    int built = 0;
    for (const auto& [id, path] : levels.entries) {
        (void)path;
        world::Level l(vfs, ini, id);
        world::WalkMap w(l);
        int open = 0;
        for (int x = 0; x < w.width(); ++x)
            for (int y = 0; y < w.height(); ++y) open += w.at(x, y).clearance != world::WalkMap::kBlocked;
        CHECK(open > 0);
        ++built;
    }
    std::printf("walk grids built for %d levels\n", built);
}

int main() {
    testFacing();
    testWalkMap();
    testAnimGroup();
    if (const char* dir = std::getenv("HARBINGER_DATA"))
        testRealData(dir);
    else
        std::printf("HARBINGER_DATA not set; skipping real-data checks\n");
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
