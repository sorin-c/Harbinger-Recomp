#include "world/item_catalog.hpp"

#include <vector>

#include "world/item_text.hpp"

namespace harbinger::world {

namespace {

mgr::Index readIndex(Vfs& vfs, const Ini& ini, const char* key) {
    const auto data = vfs.read(ini.require("Paths", key));
    return data ? mgr::parse(*data) : mgr::Index{};
}

}  // namespace

ItemCatalog::ItemCatalog(Vfs& vfs, const Ini& ini)
    : vfs_(vfs), items_(readIndex(vfs, ini, "Items")), weapons_(readIndex(vfs, ini, "Weapons")) {}

const itm::Item* ItemCatalog::item(uint16_t id) {
    auto it = itemCache_.find(id);
    if (it == itemCache_.end()) {
        std::optional<itm::Item> item;
        if (const std::string* path = items_.find(id))
            if (const auto bytes = vfs_.read(*path)) {
                try {
                    item = itm::parse(*bytes);
                } catch (const std::exception&) {
                    try {
                        item = itm::parseHeader(*bytes);  // a class whose body is not decoded
                    } catch (const std::exception&) {
                    }
                }
            }
        it = itemCache_.emplace(id, std::move(item)).first;
    }
    return it->second ? &*it->second : nullptr;
}

const ItemInfo* ItemCatalog::info(uint16_t id) {
    auto it = infoCache_.find(id);
    if (it == infoCache_.end()) {
        std::optional<ItemInfo> info;
        if (const itm::Item* i = item(id)) info = describe(id, *i);
        it = infoCache_.emplace(id, std::move(info)).first;
    }
    return it->second ? &*it->second : nullptr;
}

const wpn::Weapon* ItemCatalog::weaponOf(uint16_t itemId) {
    const itm::Item* i = item(itemId);
    if (!i || !i->socketed) return nullptr;
    const uint16_t w = i->socketed->word38;
    auto it = weaponCache_.find(w);
    if (it == weaponCache_.end()) {
        std::optional<wpn::Weapon> weapon;
        if (const std::string* path = weapons_.find(w))
            if (const auto bytes = vfs_.read(*path)) {
                try {
                    weapon = wpn::parse(*bytes);
                } catch (const std::exception&) {
                }
            }
        it = weaponCache_.emplace(w, std::move(weapon)).first;
    }
    return it->second ? &*it->second : nullptr;
}

std::string ItemCatalog::description(const Carried& c) {
    const itm::Item* i = item(c.id);
    if (!i) return {};
    std::vector<itm::Item> chips;
    for (uint16_t chip : c.chips)
        if (const itm::Item* ci = chip ? item(chip) : nullptr) chips.push_back(*ci);
    return itemDescription(*i, weaponOf(c.id), chips);
}

}  // namespace harbinger::world
