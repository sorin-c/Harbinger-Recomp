// The player character's experience, level and skills: experience from
// kills and scripts, level-ups with bonus points, the four skills each
// character shows on its stats panel, and what they change (attack ratings,
// hit points). Notes and evidence: docs/stats.md
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace harbinger::world {

enum class Character;

// The skill array at plug+0x23c (seven u16), in the order of the name
// table at 0x54fed0 ("Melee" + i * 0x20, FUN_00452790).
enum class Skill : uint8_t { Melee, Ranged, Radial, Rigging, Override, Focus, Life, Count };
constexpr int kSkillCount = int(Skill::Count);
const char* skillName(Skill s);

class Stats {
public:
    // A new character: level 1, no experience, no points, and the skills
    // its plug's constructor sets (Human FUN_004f26c0, Gladiator
    // FUN_004baa80, Culibine FUN_0046fe50).
    explicit Stats(Character c);

    Character character() const { return character_; }
    uint32_t experience() const { return experience_; }  // plug+0x234
    uint16_t level() const { return level_; }            // plug+0x24c, from 1
    uint16_t points() const { return points_; }          // plug+0x24a, bonus points to spend
    uint16_t skill(Skill s) const { return skills_[size_t(s)]; }

    // The skills the character's stats panel lists, top to bottom (slot
    // 119: GHumanPlayerPlug_v119 0x4f2c60 and the others' slot 119).
    std::vector<Skill> shown() const;
    bool shows(Skill s) const;

    // Experience needed for the next level (FUN_004f1950: the table at
    // 0x556b9c by level; level 1 needs 1000), and how far the character is from the last
    // threshold to that one, 0 to 1 (FUN_004f18f0).
    uint32_t nextLevelAt() const { return threshold(level_); }
    double levelProgress() const;
    static uint32_t threshold(uint16_t level);
    static constexpr uint16_t kMaxLevel = 36;     // [36] is 0xffffffff (see addExperience)
    static constexpr uint16_t kPointsPerLevel = 3;
    static constexpr uint16_t kMaxSkill = 50;     // GGladPlayerPlug_v120: 0x32

    // FUN_004ef480: adds experience (saturating at 0xffffffff) and takes
    // every level it reaches, 3 points each. Returns the levels gained;
    // the caller does the level-up's other work (Player::gainExperience).
    int addExperience(uint32_t amount);
    // Slot 120 (GGladPlayerPlug_v120): raises a skill the character has by
    // `amount`, up to 50. Returns the amount added (0 if none).
    int raise(Skill s, int amount);
    // Slot 115 (GGladPlayerPlug_v115, a stats panel plus button): spends a
    // point on the skill. False if there is no point or the skill is full.
    bool spendPoint(Skill s);
    bool canSpend(Skill s) const { return points_ > 0 && shows(s) && skill(s) < kMaxSkill; }

    // Hit points at full health: 2 x (Life + 20) (slot 39,
    // GGladPlayerPlug_v39, called with plug+0x248).
    double maxHitPoints() const { return 2.0 * (skill(Skill::Life) + 20); }
    // The attack ratings the rolls use (FUN_00452290): the Melee and
    // Ranged skills.
    int meleeRating() const { return skill(Skill::Melee); }
    int rangedRating() const { return skill(Skill::Ranged); }

    // plug+0x214: cleared at each level-up, set when the stats panel is
    // opened (GCtrlStatsThreeStateBtn_v7). While false the inventory's CHAR
    // button blinks (GCtrlInvThreeStateBtn_v8).
    bool levelSeen() const { return levelSeen_; }
    void setLevelSeen() { levelSeen_ = true; }

    // A killed enemy's experience (GAgentPlug_v61, FUN_00420a00): the
    // template's experience word (template+0x214, stat 8) plus its full hit
    // points (plug+0x90), but nothing at all when the word is 0.
    static uint32_t killExperience(uint16_t templateExperience, double maxHitPoints);

private:
    Character character_;
    uint32_t experience_ = 0;
    uint16_t level_ = 1, points_ = 0;
    std::array<uint16_t, kSkillCount> skills_{};
    bool levelSeen_ = true;
};

// The stats panel's tooltip for a skill (slot 116, FUN_004f19e0), with the
// equipment's bonus to it (FUN_004f25e0) and the full hit points for Life.
std::string skillTip(Skill s, const Stats& stats, int bonus);
// A plus button's tooltip (slot 117): "Increase Melee skill".
std::string plusTip(Skill s);
// The next level bar's: "Exp: 0 / 1000" (GCtrlStatsNextLevel_v10).
std::string experienceTip(const Stats& stats);

// Text the original shows for these events (FUN_004f00d0, FUN_004a5310).
std::string levelUpMessage(uint16_t level);
std::string experienceMessage(uint32_t amount);

}  // namespace harbinger::world
