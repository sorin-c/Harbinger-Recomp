#include "world/loot.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace harbinger::world {

namespace {

bool isChest(const giz::Template& t) {
    return t.cls == giz::Chest || t.cls == giz::Barrel || t.cls == giz::TrashPile;
}

mgr::Index readIndex(Vfs& vfs, const Ini& ini, const char* section, const char* key) {
    const auto path = ini.get(section, key);
    if (!path) return {};
    const auto bytes = vfs.read(*path);
    return bytes ? mgr::parse(*bytes) : mgr::Index{};
}

}  // namespace

std::optional<ChestContents> chestContents(const giz::Template& t, const chk::GizmoRecord& g) {
    // The record ends with the three words and the state (giz::Template::recordExtra).
    if (!isChest(t) || g.extra.size() < 8) return std::nullopt;
    const auto& d = g.extra;
    auto word = [&](size_t back) { return uint16_t(d[d.size() - back] | d[d.size() - back + 1] << 8); };
    return ChestContents{word(8), word(6), word(4)};
}

std::vector<Drop> rollDrops(const gig::Group* group, uint16_t guaranteed, const std::function<bool(uint16_t)>& isMoney,
                            Rng& rng) {
    std::vector<Drop> out;
    // GMoneyPlug+0x38: moneyMin + rand % (moneyMax - moneyMin), or moneyMin
    // when they are equal (the range leaves out moneyMax).
    auto amount = [&](uint16_t item) -> uint32_t {
        if (!isMoney(item)) return 0;
        const uint32_t lo = group ? group->moneyMin : 1, hi = group ? group->moneyMax : 1;
        return hi > lo ? lo + rng.next() % (hi - lo) : lo;
    };
    bool needGuaranteed = guaranteed != 0;
    if (group) {
        // FUN_004d6f30: the count. FUN_00441dc0: rand % total weight, then
        // the first entry whose running total reaches it.
        uint32_t count = group->countMin;
        if (group->countMax > group->countMin) count += rng.next() % uint32_t(group->countMax - group->countMin + 1);
        const uint32_t total = group->totalWeight();
        for (uint32_t i = 0; i < count && total > 0; ++i) {
            const uint32_t r = rng.next() % total;
            uint32_t sum = 0;
            uint16_t item = 0;
            for (const auto& e : group->entries) {
                sum += e.weight;
                if (r <= sum) {
                    item = e.item;
                    break;
                }
            }
            if (!item) break;
            if (item == guaranteed) needGuaranteed = false;
            out.push_back({item, amount(item)});
        }
    }
    if (needGuaranteed) out.push_back({guaranteed, amount(guaranteed)});
    return out;
}

Loot::Loot(Vfs& vfs, const Ini& ini, Character character, Level& level, const WalkMap& walk, Rng& rng)
    : vfs_(vfs), level_(level), walk_(walk), rng_(rng),
      // FUN_004d7f90 loads ItemGroups from the playing character's section.
      groups_(readIndex(vfs, ini, characterSection(character), "ItemGroups")),
      items_(readIndex(vfs, ini, "Paths", "Items")) {
    for (const auto& g : level.map().gizmos)
        if (isChest(level.gizmoTemplate(g))) chestStates_[g.instance] = level.gizmoState(g).state;
}

const itm::Item* Loot::item(uint16_t id) {
    auto it = itemCache_.find(id);
    if (it == itemCache_.end()) {
        std::optional<itm::Item> item;
        if (const std::string* path = items_.find(id))
            if (const auto bytes = vfs_.read(*path)) try {
                    item = itm::parseHeader(*bytes);
                } catch (const std::exception&) {
                }
        it = itemCache_.emplace(id, std::move(item)).first;
    }
    return it->second ? &*it->second : nullptr;
}

void Loot::update(uint64_t nowMs) {
    for (auto& [instance, last] : chestStates_) {
        const chk::GizmoRecord* g = level_.gizmoByInstance(instance);
        if (!g) continue;
        const uint16_t now = level_.gizmoState(*g).state;
        if (last == 2 && now == 3)
            if (const auto c = chestContents(level_.gizmoTemplate(*g), *g)) drop(*g, *c, nowMs);
        last = now;
    }
}

void Loot::drop(const chk::GizmoRecord& g, const ChestContents& c, uint64_t nowMs) {
    const gig::Group* group = nullptr;
    if (c.group) {
        auto it = groupCache_.find(c.group);
        if (it == groupCache_.end()) {
            std::optional<gig::Group> parsed;
            if (const std::string* path = groups_.find(c.group))
                if (const auto bytes = vfs_.read(*path)) try {
                        parsed = gig::parse(*bytes);
                    } catch (const std::exception&) {
                    }
            it = groupCache_.emplace(c.group, std::move(parsed)).first;
        }
        if (it->second) group = &*it->second;
    }
    const auto drops = rollDrops(group, c.item, [&](uint16_t id) {
        const itm::Item* i = item(id);
        return i && i->classId == itm::Money;
    }, rng_);

    // Where they land: the original lays items out on a pattern of half
    // cells beside the chest (FUN_004d9500, tables 0x57eac8 / 0x57eae0),
    // not rebuilt yet. The rebuild takes the free walkable half cells
    // nearest the chest's use point.
    const auto origin = level_.gizmoUsePoint(g);
    const int ox = int(std::floor(origin.x * 2)), oy = int(std::floor(origin.y * 2));
    std::vector<std::pair<double, std::pair<int, int>>> spots;
    for (int dx = -6; dx <= 6; ++dx)
        for (int dy = -6; dy <= 6; ++dy) {
            const int sx = ox + dx, sy = oy + dy;
            if (!walk_.passable(sx, sy, 1)) continue;
            const double cx = (sx + 0.5) / 2, cy = (sy + 0.5) / 2;
            bool taken = false;
            for (const auto& gi : ground_)
                if (int(std::floor(gi.x * 2)) == sx && int(std::floor(gi.y * 2)) == sy) taken = true;
            if (!taken) spots.push_back({std::hypot(cx - origin.x, cy - origin.y), {sx, sy}});
        }
    std::sort(spots.begin(), spots.end());
    uint64_t at = nowMs;
    for (size_t i = 0; i < drops.size(); ++i) {
        GroundItem gi;
        gi.id = nextId_++;
        gi.drop = drops[i];
        const auto spot = i < spots.size() ? spots[i].second : std::make_pair(ox, oy);
        gi.x = (spot.first + 0.5) / 2, gi.y = (spot.second + 0.5) / 2;
        // FUN_004d8dd0 queues each item 500 ms after the one before.
        gi.showMs = at;
        at += kDropMs;
        gi.item = item(gi.drop.item);
        ground_.push_back(gi);
    }
}

const Loot::GroundItem* Loot::find(uint32_t id) const {
    for (const auto& g : ground_)
        if (g.id == id) return &g;
    return nullptr;
}

const Loot::GroundItem* Loot::nearest(double x, double y, double range, uint64_t nowMs) const {
    const GroundItem* best = nullptr;
    double bestD = range;
    for (const auto& g : ground_) {
        if (!landed(g, nowMs)) continue;
        const double d = std::hypot(g.x - x, g.y - y);
        if (d <= bestD) bestD = d, best = &g;
    }
    return best;
}

std::optional<Drop> Loot::take(uint32_t id) {
    for (auto it = ground_.begin(); it != ground_.end(); ++it)
        if (it->id == id) {
            const Drop d = it->drop;
            ground_.erase(it);
            return d;
        }
    return std::nullopt;
}

}  // namespace harbinger::world
