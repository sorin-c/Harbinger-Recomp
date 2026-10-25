#include "world/inventory.hpp"

#include <algorithm>
#include <limits>

namespace harbinger::world {

bool ItemInfo::covers(int x, int y) const {
    // FUN_004d5a10: outside the 8 x 8 mask, or without one, every cell counts.
    if (!mask || x < 0 || y < 0 || x >= 8 || y >= 8) return true;
    return ((*mask)[size_t(y)] >> x) & 1;
}

ItemInfo describe(uint16_t id, const itm::Item& item) {
    ItemInfo i;
    i.id = id;
    i.classId = item.classId;
    i.name = item.name;
    i.w = uint8_t(std::clamp<int>(item.unknown5, 1, 8));
    i.h = uint8_t(std::clamp<int>(item.unknown6, 1, 8));
    if (item.block) {
        std::array<uint8_t, 8> m{};
        std::copy_n(item.block->begin(), 8, m.begin());
        i.mask = m;
    }
    i.kind = itm::equipKind(item);
    if (item.socketed) i.sockets = item.socketed->byte3c;
    i.money = item.classId == itm::Money;
    i.healthPack = item.classId >= itm::HumanHealth && item.classId <= itm::CulibineHealth;
    return i;
}

Inventory::Inventory(Layout layout, Lookup lookup)
    : layout_(std::move(layout)), lookup_(std::move(lookup)), slots_(layout_.slots.size()),
      packs_(layout_.healthPacks) {}

bool Inventory::locked(uint16_t kind) {
    switch (kind) {
        case 1:
        case 5:
        case 9:
        case 0xb:
        case 0xd:
        case 0xf:
        case 0x11: return true;
        default: return false;
    }
}

int Inventory::itemAt(int x, int y) const {
    for (size_t k = 0; k < grid_.size(); ++k) {
        const Placed& p = grid_[k];
        const ItemInfo* in = info(p.item.id);
        if (!in) continue;
        const int dx = x - p.x, dy = y - p.y;
        if (dx >= 0 && dy >= 0 && dx < in->w && dy < in->h && in->covers(dx, dy)) return int(k);
    }
    return -1;
}

Inventory::Fit Inventory::fit(uint16_t id, int x, int y, int* swap) const {
    const ItemInfo* in = info(id);
    if (swap) *swap = -1;
    if (!in || x < 0 || y < 0 || x + in->w > gridW() || y + in->h > gridH()) return Fit::No;
    int other = -1;
    for (int dx = 0; dx < in->w; ++dx)
        for (int dy = 0; dy < in->h; ++dy) {
            if (!in->covers(dx, dy)) continue;
            const int k = itemAt(x + dx, y + dy);
            if (k < 0) continue;
            if (other >= 0 && k != other) return Fit::No;  // two items under it
            other = k;
        }
    if (swap) *swap = other;
    return other < 0 ? Fit::Free : Fit::Swap;
}

std::pair<int, int> Inventory::dropCell(uint16_t id, int x, int y) const {
    const ItemInfo* in = info(id);
    const int w = in ? in->w : 1, h = in ? in->h : 1;
    x -= w >> 1, y -= h >> 1;
    x = x < 0 ? 0 : std::min(x, gridW() - w);
    y = y < 0 ? 0 : std::min(y, gridH() - h);
    return {x, y};
}

bool Inventory::autoPlace(const Carried& item) {
    const ItemInfo* in = info(item.id);
    if (!in) return false;
    for (int y = 0; y + in->h <= gridH(); ++y)
        for (int x = 0; x + in->w <= gridW(); ++x)
            if (fit(item.id, x, y) == Fit::Free) {
                grid_.push_back({item, x, y});
                return true;
            }
    return false;
}

Inventory::Added Inventory::add(const Carried& item, uint32_t amount) {
    const ItemInfo* in = info(item.id);
    if (!in) return Added::NoRoom;
    if (in->money) {
        const uint32_t room = std::numeric_limits<uint32_t>::max() - money_;
        money_ += std::min(room, amount);
        return Added::Money;
    }
    if (in->healthPack && packs_ < layout_.maxHealthPacks) {
        ++packs_;
        return Added::HealthPack;
    }
    if (in->kind) {
        for (size_t i = 0; i < slotCount(); ++i)
            if (slot(i).parent < 0 && !slots_[i] && accepts(i, item.id)) {
                put(i, item);
                return Added::Equipped;
            }
        for (size_t i = 0; i < slotCount(); ++i)
            if (slot(i).parent >= 0 && !equipped(i) && accepts(i, item.id)) {
                put(i, item);
                return Added::Socketed;
            }
    }
    return autoPlace(item) ? Added::Grid : Added::NoRoom;
}

int Inventory::socketIndex(size_t i) const {
    int n = 0;
    for (size_t k = 0; k < i; ++k)
        if (slot(k).parent == slot(i).parent) ++n;
    return n;
}

bool Inventory::slotEnabled(size_t i) const {
    const SlotDef& s = slot(i);
    if (s.parent < 0) return true;
    const auto& owner = slots_.at(size_t(s.parent));
    const ItemInfo* in = owner ? info(owner->id) : nullptr;
    return in && socketIndex(i) < in->sockets;
}

std::optional<Carried> Inventory::equipped(size_t i) const {
    const SlotDef& s = slot(i);
    if (s.parent < 0) return slots_.at(i);
    const auto& owner = slots_.at(size_t(s.parent));
    const size_t k = size_t(socketIndex(i));
    if (!owner || k >= owner->chips.size() || owner->chips[k] == 0) return std::nullopt;
    return Carried{owner->chips[k], {}};
}

bool Inventory::accepts(size_t i, uint16_t id) const {
    const ItemInfo* in = info(id);
    if (!in || !in->kind || !slotEnabled(i)) return false;
    // The drop item cells (7 for the Gladiator, 0x16 for the Human) take
    // both kinds of drop item, cameras (7) and mines (0x16).
    auto drop = [](uint16_t k) { return k == 7 || k == 0x16; };
    const uint16_t k = slot(i).kind;
    return in->kind == k || (drop(k) && drop(in->kind));
}

void Inventory::put(size_t i, Carried item) {
    const SlotDef& s = slot(i);
    if (s.parent < 0) {
        slots_.at(i) = std::move(item);
    } else {
        auto& owner = slots_.at(size_t(s.parent));
        const size_t k = size_t(socketIndex(i));
        if (owner->chips.size() <= k) owner->chips.resize(k + 1, 0);
        owner->chips[k] = item.id;
    }
    ++equipRev_;
}

std::optional<Carried> Inventory::take(size_t i) {
    std::optional<Carried> out = equipped(i);
    if (!out) return out;
    const SlotDef& s = slot(i);
    if (s.parent < 0) slots_.at(i).reset();
    else slots_.at(size_t(s.parent))->chips[size_t(socketIndex(i))] = 0;
    ++equipRev_;
    return out;
}

std::vector<uint16_t> Inventory::equippedIds() const {
    std::vector<uint16_t> ids;
    for (size_t i = 0; i < slotCount(); ++i)
        if (slot(i).parent < 0 && slots_[i]) ids.push_back(slots_[i]->id);
    return ids;
}

std::vector<uint16_t> Inventory::chipsOf(size_t i) const {
    std::vector<uint16_t> out;
    if (i < slotCount() && slots_[i])
        for (uint16_t c : slots_[i]->chips)
            if (c) out.push_back(c);
    return out;
}

Inventory::Click Inventory::clickGrid(int x, int y) {
    if (!held_) {
        const int k = itemAt(x, y);
        if (k < 0) return Click::Nothing;
        held_ = grid_[size_t(k)].item;
        grid_.erase(grid_.begin() + k);
        return Click::Took;
    }
    const auto [cx, cy] = dropCell(held_->id, x, y);
    int swap = -1;
    const Fit f = fit(held_->id, cx, cy, &swap);
    if (f == Fit::No) return Click::Nothing;
    std::optional<Carried> displaced;
    if (f == Fit::Swap) {
        displaced = grid_[size_t(swap)].item;
        grid_.erase(grid_.begin() + swap);
    }
    grid_.push_back({*held_, cx, cy});
    held_ = displaced;
    return displaced ? Click::Swapped : Click::Put;
}

Inventory::Click Inventory::clickSlot(size_t i) {
    if (i >= slotCount()) return Click::Nothing;
    if (!held_) {
        if (!equipped(i) || (slot(i).parent < 0 && locked(slot(i).kind))) return Click::Nothing;
        held_ = take(i);
        return Click::Took;
    }
    if (!accepts(i, held_->id)) return Click::Nothing;
    std::optional<Carried> old = take(i);
    put(i, *held_);
    held_ = old;
    return old ? Click::Swapped : Click::Put;
}

bool Inventory::stowHeld() {
    if (!held_) return true;
    if (!autoPlace(*held_)) return false;
    held_.reset();
    return true;
}

bool Inventory::takeHealthPack() {
    if (!packs_) return false;
    --packs_;
    // FUN_004cd090: a pack carried in the grid moves onto the counter.
    for (auto it = grid_.begin(); it != grid_.end(); ++it)
        if (const ItemInfo* i = info(it->item.id); i && i->healthPack) {
            grid_.erase(it);
            ++packs_;
            break;
        }
    return true;
}

}  // namespace harbinger::world
