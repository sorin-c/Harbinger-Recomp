// Tests for experience, levels and skills: the threshold table, level-ups
// and their points, spending points, the skill cap, hit points from Life,
// kill experience and the tooltip texts. Synthetic cases always run; with
// HARBINGER_DATA set, the agent templates' experience words are checked and
// the Human kills an enemy in Storage (level 3) for its experience.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/agt.hpp"
#include "formats/mgr.hpp"
#include "world/foes.hpp"
#include "world/level.hpp"
#include "world/player.hpp"
#include "world/stats.hpp"
#include "world/walkmap.hpp"

using namespace harbinger;
using world::Character;
using world::Skill;
using world::Stats;

static int failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

static void testNewCharacters() {
    // The plugs' constructors, and what the original's stats panel shows
    // for a new Human: Melee 15, Ranged 15, Rigging 10, Life 15, level 1.
    const Stats h(Character::Human);
    CHECK(h.level() == 1 && h.experience() == 0 && h.points() == 0 && h.levelSeen());
    CHECK(h.skill(Skill::Melee) == 15 && h.skill(Skill::Ranged) == 15 && h.skill(Skill::Rigging) == 10 &&
          h.skill(Skill::Life) == 15);
    CHECK((h.shown() == std::vector<Skill>{Skill::Melee, Skill::Ranged, Skill::Rigging, Skill::Life}));
    const Stats g(Character::Gladiator), c(Character::Culibine);
    CHECK((g.shown() == std::vector<Skill>{Skill::Melee, Skill::Ranged, Skill::Override, Skill::Life}));
    CHECK((c.shown() == std::vector<Skill>{Skill::Ranged, Skill::Radial, Skill::Focus, Skill::Life}));
    CHECK(g.skill(Skill::Override) == 10 && c.skill(Skill::Focus) == 15 && c.skill(Skill::Melee) == 0);
    // 2 x (Life + 20): the hit points PR #8 had from the constructors.
    CHECK(h.maxHitPoints() == 70 && g.maxHitPoints() == 80 && c.maxHitPoints() == 60);
    CHECK(h.meleeRating() == 15 && c.rangedRating() == 15);
}

static void testLevels() {
    CHECK(Stats::threshold(1) == 1000 && Stats::threshold(2) == 2000 && Stats::threshold(3) == 3500);
    CHECK(Stats::threshold(35) == 900000 && Stats::threshold(36) == 0xffffffffu);
    Stats s(Character::Human);
    CHECK(s.addExperience(999) == 0 && s.level() == 1);
    CHECK(s.levelProgress() > 0.99 && s.levelProgress() < 1.0);
    CHECK(s.addExperience(1) == 1 && s.level() == 2 && s.points() == 3 && !s.levelSeen());
    CHECK(s.levelProgress() == 0.0);
    s.addExperience(500);
    CHECK(s.levelProgress() == 0.5);  // (1500 - 1000) / (2000 - 1000)
    // Several levels at once, 3 points each.
    CHECK(s.addExperience(8500) == 4 && s.level() == 6 && s.points() == 15);  // 10000 passes [5] too
    // Level 36 is the last; the experience saturates.
    s.addExperience(0xfffffff0u);
    CHECK(s.level() == 36 && s.experience() == 0xffffffffu);
    s.setLevelSeen();
    CHECK(s.levelSeen());
    CHECK(world::experienceTip(Stats(Character::Human)) == "Exp: 0 / 1000");
    CHECK(world::levelUpMessage(2) == "You've reached level 2!");
    CHECK(world::experienceMessage(250) == "250 Experience Awarded.");
}

static void testSkills() {
    Stats s(Character::Human);
    CHECK(!s.spendPoint(Skill::Melee));  // no points
    s.addExperience(1000);
    CHECK(!s.spendPoint(Skill::Radial));  // not a Human skill
    CHECK(s.spendPoint(Skill::Life) && s.skill(Skill::Life) == 16 && s.points() == 2);
    CHECK(s.maxHitPoints() == 72);
    // Skills stop at 50.
    CHECK(s.raise(Skill::Melee, 100) == 35 && s.skill(Skill::Melee) == 50);
    CHECK(!s.canSpend(Skill::Melee) && !s.spendPoint(Skill::Melee) && s.points() == 2);
    CHECK(s.raise(Skill::Melee, 1) == 0);
    // The panel's tooltips, as the original shows them for a new Human.
    const Stats h(Character::Human);
    CHECK(world::skillTip(Skill::Melee, h, 0) == "Melee chance to hit +15%");
    CHECK(world::skillTip(Skill::Ranged, h, 2) == "Ranged chance to hit +15% (+2)");
    CHECK(world::skillTip(Skill::Life, h, 0) == "70 full health hit points");
    CHECK(world::skillTip(Skill::Rigging, h, 0) == "Able to use gadgets that\nrequire rigging skill 10\nor below");
    CHECK(world::plusTip(Skill::Rigging) == "Increase Rigging skill");
    // Kill experience: the template's word plus the enemy's full hit points,
    // and nothing when the word is 0.
    CHECK(Stats::killExperience(25, 28) == 53);
    CHECK(Stats::killExperience(0, 40) == 0);
}

static void testRealData(const char* dir) {
    Vfs vfs(dir);
    const Ini ini = Ini::fromBytes(*vfs.read("Harbinger.ini"));
    // Stat 8 is the experience word (template+0x214, FUN_00406a60): 25 for
    // Vantir Infantry, 10000 for the Overlord, 0 for the player and NPCs.
    const auto agents = mgr::parse(*vfs.read(ini.require("Paths", "Agents")));
    int withXp = 0;
    for (const auto& [id, path] : agents.entries) {
        const auto data = vfs.read(path);
        if (!data) continue;
        const auto t = agt::parse(*data);
        if (t.stats.size() > 8 && t.stats[8]) ++withXp;
        if (t.name == "Vantir Infantry") CHECK(t.stats.size() > 8 && t.stats[8] == 25);
        if (t.name == "Overlord") CHECK(t.stats.size() > 8 && t.stats[8] == 10000);
        if (t.isNpc() && t.stats.size() > 8) CHECK(t.stats[8] <= 1);
    }
    std::printf("%d agent templates give experience\n", withXp);
    CHECK(withXp > 60);
    const auto dude = agt::parse(*vfs.read(ini.require("Human", "Agent")));
    CHECK(dude.stats.size() > 8 && dude.stats[8] == 0);

    // The player's hit points and ratings follow the skills; a level-up
    // heals and refills the rifle.
    world::Level level(vfs, ini, 3);
    world::WalkMap walk(level);
    world::Player player(vfs, ini, Character::Human, walk, level.map().entries[0]);
    CHECK(player.maxHitPoints() == 70 && player.meleeRating() == 15 && player.rangedRating() == 15);
    player.takeDamage(30, 0);
    CHECK(player.hitPoints() == 40);
    CHECK(player.gainExperience(1000) == 1);
    CHECK(player.hitPoints() == 70 && player.energy() == player.maxEnergy() && player.stats().points() == 3);
    CHECK(player.spendPoint(Skill::Life) && player.maxHitPoints() == 72 && player.hitPoints() == 70);
    CHECK(player.spendPoint(Skill::Melee) && player.meleeRating() == 16);
}

int main() {
    testNewCharacters();
    testLevels();
    testSkills();
    if (const char* dir = std::getenv("HARBINGER_DATA"))
        testRealData(dir);
    else
        std::printf("HARBINGER_DATA not set; skipping real-data checks\n");
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
