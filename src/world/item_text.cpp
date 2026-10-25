#include "world/item_text.hpp"

#include <algorithm>
#include <cstdio>

#include "world/combat.hpp"

namespace harbinger::world {

const char* damageTypeName(uint16_t type) {
    static const char* const kNames[] = {"Melee", "Plasma",   "EMP",       "Electricity", "Disruption",
                                         "Narcotic", "Decay", "Corrosion", "Nerve Toxin", "Armor Class"};
    return type < 10 ? kNames[type] : "";
}

namespace {

// What a chip's socket target is called on its second line (only "Weapon"
// is seen in the original; the others are the strings next to it).
const char* targetName(uint16_t target) {
    switch (target) {
        case 1: return "Armor";
        case 2: return "Helmet";
        case 3: return "Weapon";
        case 4: return "Torso";
        case 5: return "Ranged Weapon";
        case 6: return "Melee Weapon";
        case 7: return "Left Gauntlet";
        case 8: return "Right Gauntlet";
        default: return "";
    }
}

std::string damageLine(bool boosted, uint16_t type, uint16_t level, uint16_t extra) {
    const DamageRange r = damageRange(type, level);
    char buf[160];
    if (extra)
        std::snprintf(buf, sizeof buf, "%s   %s: Lvl. %u (%u - %u) (+%u)", boosted ? "&" : "", damageTypeName(type),
                      unsigned(level), unsigned(r.min), unsigned(r.max), unsigned(extra));
    else
        std::snprintf(buf, sizeof buf, "%s   %s: Lvl. %u (%u - %u)", boosted ? "&" : "", damageTypeName(type),
                      unsigned(level), unsigned(r.min), unsigned(r.max));
    return buf;
}

}  // namespace

std::string itemDescription(const itm::Item& item, const wpn::Weapon* weapon, const std::vector<itm::Item>& chips) {
    std::string text = item.name;
    auto line = [&](const std::string& s) { text += "\n" + s; };
    if (item.modChip) {
        // "EMP mod chip (+1)" / "Weapon".
        const auto& m = *item.modChip;
        std::string first = std::string(damageTypeName(m.word38)) + " mod chip";
        if (m.word3a) first += " (+" + std::to_string(m.word3a) + ")";
        text = first;
        line(targetName(m.target));
        return text;
    }
    if (item.socketed && item.socketed->byte3c) line("Slots: " + std::to_string(item.socketed->byte3c));
    if (!weapon) return text;
    if (weapon->kind == 4) {
        const wpn::Damage* ac = weapon->find(wpn::ArmourClass);
        line("AC: " + std::to_string(ac ? ac->level : 0));
        return text;
    }
    // Damage by type in the weapon's order; a chip raises a type the weapon
    // has, or adds one it lacks, one level above the weapon's best ranged
    // level (guessed: the one tooltip seen, the E-16's EMP chip giving
    // "EMP: Lvl. 6 (+1)" next to "Plasma: Lvl. 5").
    struct Row {
        uint16_t type, level, extra;
        bool boosted;
    };
    std::vector<Row> rows;
    for (const auto& d : weapon->damage)
        if (d.type < 9) rows.push_back({d.type, d.level, d.extra, false});
    uint16_t best = 0;
    for (const auto& d : weapon->damage)
        if (d.type >= 1 && d.type <= 4) best = std::max(best, d.level);
    for (const auto& c : chips) {
        if (!c.modChip || c.modChip->word38 >= 9) continue;
        const auto& m = *c.modChip;
        auto it = std::find_if(rows.begin(), rows.end(), [&](const Row& r) { return r.type == m.word38; });
        if (it != rows.end()) {
            it->level = uint16_t(it->level + m.word3a);
            it->extra = uint16_t(it->extra + m.word3c);
            it->boosted = true;
        } else {
            rows.push_back({m.word38, uint16_t(best + m.word3a), m.word3c, true});
        }
    }
    if (!rows.empty()) line("Damage:");
    for (const Row& r : rows) line(damageLine(r.boosted, r.type, r.level, r.extra));
    return text;
}

std::string healthPackText(int count) {
    return std::to_string(count) + (count == 1 ? " Health Pack" : " Health Packs");
}

}  // namespace harbinger::world
