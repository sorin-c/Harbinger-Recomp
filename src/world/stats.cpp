#include "world/stats.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "world/player.hpp"

namespace harbinger::world {

namespace {

// Experience to leave each level: the u32 table at 0x556b9c, read at
// [level] (level 1 needs 1000 to reach 2; 35 needs 900000; [36] is
// 0xffffffff, so nothing passes it).
constexpr uint32_t kThresholds[] = {
    0,      1000,   2000,   3500,   5000,   10000,  15000,  20000,  25000,  30000,  40000,  50000,  65000,
    80000,  100000, 125000, 150000, 175000, 200000, 225000, 250000, 275000, 300000, 325000, 350000, 375000,
    400000, 425000, 450000, 500000, 550000, 600000, 650000, 700000, 800000, 900000, 0xffffffffu,
};

}  // namespace

const char* skillName(Skill s) {
    static const char* const kNames[] = {"Melee", "Ranged", "Radial", "Rigging", "Override", "Focus", "Life"};
    return size_t(s) < std::size(kNames) ? kNames[size_t(s)] : "";
}

Stats::Stats(Character c) : character_(c) {
    auto set = [&](Skill s, uint16_t v) { skills_[size_t(s)] = v; };
    switch (c) {
        case Character::Human:  // FUN_004f26c0: +0x23c, +0x23e = 15, +0x242 = 10, +0x248 = 15
            set(Skill::Melee, 15), set(Skill::Ranged, 15), set(Skill::Rigging, 10), set(Skill::Life, 15);
            break;
        case Character::Gladiator:  // FUN_004baa80: +0x23c, +0x23e = 15, +0x244 = 10, +0x248 = 20
            set(Skill::Melee, 15), set(Skill::Ranged, 15), set(Skill::Override, 10), set(Skill::Life, 20);
            break;
        case Character::Culibine:  // FUN_0046fe50: +0x23e, +0x240, +0x246 = 15, +0x248 = 10
            set(Skill::Ranged, 15), set(Skill::Radial, 15), set(Skill::Focus, 15), set(Skill::Life, 10);
            break;
    }
}

std::vector<Skill> Stats::shown() const {
    switch (character_) {
        case Character::Human: return {Skill::Melee, Skill::Ranged, Skill::Rigging, Skill::Life};
        case Character::Gladiator: return {Skill::Melee, Skill::Ranged, Skill::Override, Skill::Life};
        case Character::Culibine: return {Skill::Ranged, Skill::Radial, Skill::Focus, Skill::Life};
    }
    return {};
}

bool Stats::shows(Skill s) const {
    const auto v = shown();
    return std::find(v.begin(), v.end(), s) != v.end();
}

uint32_t Stats::threshold(uint16_t level) {
    return level < std::size(kThresholds) ? kThresholds[level] : 0xffffffffu;
}

double Stats::levelProgress() const {
    // FUN_004f18f0: (experience - [level - 1]) / ([level] - [level - 1]),
    // the base being 0 at level 1.
    const uint32_t base = level_ > 1 ? threshold(uint16_t(level_ - 1)) : 0;
    const uint32_t next = threshold(level_);
    if (next <= base) return 1;
    return std::clamp(double(experience_ - std::min(experience_, base)) / double(next - base), 0.0, 1.0);
}

int Stats::addExperience(uint32_t amount) {
    experience_ = amount < 0xffffffffu - experience_ ? experience_ + amount : 0xffffffffu;
    // FUN_004ef480 takes a level while [level] <= experience. It clamps a
    // level above 36 back to 35, but [36] is 0xffffffff, so 36 is the last
    // level (only saturated experience would pass it, and loop for ever
    // in the original; the rebuild stops).
    int gained = 0;
    while (level_ < kMaxLevel && threshold(level_) <= experience_) {
        points_ = uint16_t(points_ + kPointsPerLevel);
        ++level_;
        levelSeen_ = false;
        ++gained;
    }
    return gained;
}

int Stats::raise(Skill s, int amount) {
    if (amount <= 0 || !shows(s)) return 0;
    uint16_t& v = skills_[size_t(s)];
    if (v >= kMaxSkill) return 0;
    amount = std::min(amount, int(kMaxSkill - v));
    v = uint16_t(v + amount);
    return amount;
}

bool Stats::spendPoint(Skill s) {
    if (!canSpend(s)) return false;
    --points_;
    raise(s, 1);
    return true;
}

uint32_t Stats::killExperience(uint16_t templateExperience, double maxHitPoints) {
    if (templateExperience == 0) return 0;
    return uint32_t(templateExperience) + uint32_t(std::max(0.0, maxHitPoints));
}

std::string skillTip(Skill s, const Stats& stats, int bonus) {
    const std::string v = std::to_string(stats.skill(s));
    std::string out;
    switch (s) {
        case Skill::Melee:
        case Skill::Ranged:
        case Skill::Radial: out = std::string(skillName(s)) + " chance to hit +" + v + "%"; break;
        case Skill::Rigging: out = "Able to use gadgets that\nrequire rigging skill " + v + "\nor below"; break;
        case Skill::Override: out = "Able to use cameras that\nrequire override skill " + v + "\nor below"; break;
        case Skill::Life:
            out = std::to_string(int(stats.maxHitPoints())) + " full health hit points";
            break;
        default: out = std::string(skillName(s)) + " skill: " + v; break;
    }
    // Melee, Ranged and Radial add the equipment's bonus: " (+N)".
    if ((s == Skill::Melee || s == Skill::Ranged || s == Skill::Radial) && bonus != 0)
        out += " (+" + std::to_string(bonus) + ")";
    return out;
}

std::string plusTip(Skill s) {
    return std::string("Increase ") + skillName(s) + " skill";
}

std::string experienceTip(const Stats& stats) {
    return "Exp: " + std::to_string(stats.experience()) + " / " + std::to_string(stats.nextLevelAt());
}

std::string levelUpMessage(uint16_t level) {
    return "You've reached level " + std::to_string(level) + "!";
}

std::string experienceMessage(uint32_t amount) {
    return std::to_string(amount) + " Experience Awarded.";
}

}  // namespace harbinger::world
