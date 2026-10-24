// Tests for fighting: the weapon files, the original's random numbers and
// damage arithmetic, and the player hitting an enemy until it dies.
// Synthetic cases always run; with HARBINGER_DATA set, every weapon file
// parses and a fight in Storage (level 3) runs to the enemy's death.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/mgr.hpp"
#include "formats/wpn.hpp"
#include "world/combat.hpp"
#include "world/foes.hpp"
#include "world/level.hpp"
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

static void put16(std::vector<uint8_t>& d, uint16_t v) {
    d.push_back(uint8_t(v));
    d.push_back(uint8_t(v >> 8));
}

static std::vector<uint8_t> weaponFile(double version, uint16_t kind, std::vector<wpn::Damage> damage) {
    std::vector<uint8_t> d(8);
    std::memcpy(d.data(), &version, 8);
    put16(d, kind);
    if (version > 1.1) d.push_back(2), d.push_back(0);
    put16(d, uint16_t(damage.size()));
    for (const auto& x : damage) put16(d, x.type), put16(d, x.level), put16(d, x.extra);
    return d;
}

static void testWeapon() {
    const auto blade = wpn::parse(weaponFile(1.2, 0, {{0, 7, 0}}));
    CHECK(blade.kind == 0 && blade.hitBonus == 2 && blade.find(wpn::Melee) && blade.find(wpn::Melee)->level == 7);
    // Version 1.1 has no bonus bytes (GWeapon_v7 tests > 1.1).
    const auto old = wpn::parse(weaponFile(1.1, 4, {{9, 40, 0}}));
    CHECK(old.hitBonus == 0 && old.find(wpn::ArmourClass)->level == 40);
    auto bad = weaponFile(1.2, 0, {{0, 7, 0}});
    bad.push_back(0);  // a melee weapon has nothing after its damage
    bool threw = false;
    try {
        wpn::parse(bad);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

static void testArithmetic() {
    // The damage table: the E-16's tooltip reads "Melee: Lvl. 7 (6 - 15)",
    // "Plasma: Lvl. 5 (4 - 10)", "EMP: Lvl. 6 (6 - 12)".
    CHECK(world::damageRange(0, 7).min == 6 && world::damageRange(0, 7).max == 15);
    CHECK(world::damageRange(1, 5).min == 4 && world::damageRange(1, 5).max == 10);
    CHECK(world::damageRange(2, 6).min == 6 && world::damageRange(2, 6).max == 12);
    CHECK(world::damageRange(0, 0).max == 0 && world::damageRange(0, 21).max == 0);

    // The generator: seed 0 gives halves 1 and 2.
    world::Rng rng(0);
    const uint32_t a = 18000, b = 2 * 0x78b7;
    CHECK(rng.next() == ((b & 0xffff) + (a << 16)));

    agt::Template t;
    t.stats = {5, 8, 8};
    CHECK(world::rollHitPoints(t, rng) == 40);  // the Human's 5 x 8
    t.stats = {7, 2, 3};                        // a baby biter: 7 rolls of 2 + rand % 1
    CHECK(world::rollHitPoints(t, rng) == 14);
    t.stats = {4, 5, 7};
    for (int i = 0; i < 50; ++i) {
        const int hp = world::rollHitPoints(t, rng);
        CHECK(hp >= 20 && hp <= 24);
    }

    wpn::Weapon w;
    w.damage = {{0, 7, 0}, {1, 5, 1}};
    agt::Template target;
    target.word10 = 1000;  // out of reach: only a roll of 49 or 50 hits
    target.resistances = {{0, 100}};
    int hits = 0, crits = 0;
    for (int i = 0; i < 2000; ++i) {
        const auto s = world::strike(15, w, target, rng, {wpn::Melee});
        if (s.hit) {
            ++hits;
            CHECK(s.critical && s.damage >= 12 && s.damage <= 28);  // doubled 6-14
        }
        crits += s.critical;
    }
    CHECK(hits == crits && hits > 40 && hits < 130);  // 4% of 2000
    target.word10 = 0;
    target.resistances = {{0, 50}};
    for (int i = 0; i < 200; ++i) {
        const auto s = world::strike(15, w, target, rng, {wpn::Melee});
        CHECK(s.hit && s.damage >= 3 && s.damage <= (s.critical ? 14 : 7));  // half of 6-14, at least 1
    }
    target.resistances = {};  // no type gets through: no hit
    CHECK(!world::strike(15, w, target, rng).hit);
}

static void testRealData(const char* dir) {
    Vfs vfs(dir);
    const Ini ini = Ini::fromBytes(*vfs.read("Harbinger.ini"));
    const auto weapons = mgr::parse(*vfs.read(ini.require("Paths", "Weapons")));
    int parsed = 0;
    for (const auto& [id, path] : weapons.entries) {
        auto data = vfs.read(path);
        if (!data) continue;
        const auto w = wpn::parse(*data);
        CHECK(w.kind <= 4);
        ++parsed;
    }
    CHECK(parsed > 800);
    std::printf("%d weapon files parsed\n", parsed);
    // The Human's E-16 (weapons.mgr 87) and Alpha Advance Armor (494), as the
    // original's inventory tooltips show them.
    const auto e16 = wpn::parse(*vfs.read(*weapons.find(87)));
    CHECK(e16.find(wpn::Melee)->level == 7 && e16.find(wpn::Plasma)->level == 5);
    CHECK(wpn::parse(*vfs.read(*weapons.find(494))).find(wpn::ArmourClass)->level == 40);

    // Storage: the Human walks to the nearest enemy and hits it until it dies.
    world::Level level(vfs, ini, 3);
    world::WalkMap walk(level);
    world::Rng rng(7);
    world::Foes foes(vfs, ini, level, walk, rng);
    CHECK(!foes.all().empty());
    world::Player player(vfs, ini, world::Character::Human, walk, level.map().entries[0]);
    CHECK(player.weapon().find(wpn::Melee) != nullptr);
    const int id = foes.nearest(player.x(), player.y(), 4.0);
    CHECK(id >= 0);
    if (id < 0) return;
    const auto& foe = foes.all()[size_t(id)];
    player.attack(id, foe.x, foe.y);
    int blows = 0;
    uint64_t t = 0;
    for (; t < 60000 && !foe.dead; t += 10) {
        player.trackTarget(foe.x, foe.y, !foe.dead);
        if (const auto b = player.update(t)) {
            ++blows;
            CHECK(foes.blow(b->target, player.x(), player.y(), b->x, b->y, player.meleeRating(), player.weapon(), t));
        }
    }
    CHECK(foe.dead && blows >= 1);
    // A swing is 10 frames of 100 ms and loops back 9 frames: about one blow
    // every 0.9 s after the first.
    std::printf("enemy (%d hit points) died after %d blows in %.1f s\n", foe.maxHitPoints, blows, t / 1000.0);
    CHECK(foes.pose(*foe.record, t + 5000).image != nullptr);  // the corpse stays
    // The swing ends once the target is dead, and the player stands idle.
    for (int i = 0; i < 200; ++i) {
        player.trackTarget(foe.x, foe.y, !foe.dead);
        player.update(t += 10);
    }
    CHECK(player.state() == agt::Idle && player.target() == -1);
}

// Junkyard: the enemies notice a player who stands still within sight, walk
// up to it and hit it until it dies; then they settle down.
static void testEnemies(const char* dir) {
    Vfs vfs(dir);
    const Ini ini = Ini::fromBytes(*vfs.read("Harbinger.ini"));
    world::Level level(vfs, ini, 2);
    world::WalkMap walk(level);
    world::Rng rng(11);
    world::Foes foes(vfs, ini, level, walk, rng);
    CHECK(!foes.all().empty());
    // The Human's defence is its armour's class: Alpha Advance, 40.
    world::Player probe(vfs, ini, world::Character::Human, walk, level.map().entries[0]);
    CHECK(probe.defender().defence == 40 && probe.defender().player);
    CHECK(probe.hitPoints() == 70 && probe.visibility() == 128);
    // Stand the player two cells from the first enemy that can reach it.
    int id = -1;
    chk::EntryPoint spot;
    for (size_t i = 0; i < foes.all().size() && id < 0; ++i) {
        const auto& f = foes.all()[i];
        if (!f.armed) continue;
        for (int k = 0; k < 8 && id < 0; ++k) {
            const double a = k * 0.785398;
            const double px = f.x + 2 * std::cos(a), py = f.y + 2 * std::sin(a);
            if (!walk.passableAt(px, py, 2) || walk.findPath({f.x, f.y}, {px, py}, f.limit).empty()) continue;
            spot.x = px, spot.y = py;
            id = int(i);
        }
    }
    CHECK(id >= 0);
    if (id < 0) return;
    world::Player player(vfs, ini, world::Character::Human, walk, spot);
    const auto& foe = foes.all()[size_t(id)];
    CHECK(foe.mind.role == 0 && foe.mind.emotion[2] == 127 && foe.rating > 0);
    uint64_t t = 1, firstBlow = 0, deathAt = 0;
    int blows = 0, hits = 0;
    double closest = 1e9;
    for (; t < 120000 && !player.dead(); t += 10) {
        player.update(t);
        world::Quarry q;
        q.x = player.x(), q.y = player.y(), q.radius = player.radius();
        q.visibility = player.visibility(), q.alive = !player.dead(), q.defender = &player.defender();
        for (const auto& h : foes.update(t, q)) {
            ++blows;
            if (!firstBlow) firstBlow = t;
            if (h.strike.hit) ++hits, player.takeDamage(h.strike.damage, t);
        }
        closest = std::min(closest, std::hypot(foe.x - player.x(), foe.y - player.y()));
    }
    deathAt = t;
    CHECK(blows > 0 && hits > 0);
    CHECK(closest < 1.0);
    CHECK(foe.mind.engaged && foe.mind.role == 2);
    CHECK(player.dead() && player.hitPoints() == 0 && player.visibility() == 0);
    std::printf("enemies: first blow at %.1f s, %d blows, %d hits, the player died at %.1f s\n", firstBlow / 1000.0,
                blows, hits, deathAt / 1000.0);
    CHECK(player.state() == agt::Die);
    // A dead player is no target: the foes lose it and go back to resting.
    world::Quarry gone;
    gone.x = player.x(), gone.y = player.y(), gone.radius = player.radius();
    for (uint64_t u = 0; u < 20000; u += 10) CHECK(foes.update(t + u, gone).empty());
    CHECK(!foes.all()[size_t(id)].mind.engaged && foes.all()[size_t(id)].mind.policy != world::Foes::Policy::Attack);
}

int main() {
    testWeapon();
    testArithmetic();
    if (const char* dir = std::getenv("HARBINGER_DATA"))
        testRealData(dir), testEnemies(dir);
    else
        std::printf("HARBINGER_DATA not set; skipping real-data checks\n");
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
