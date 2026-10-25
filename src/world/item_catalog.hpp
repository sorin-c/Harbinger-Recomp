// Item templates and weapon files by id, read once: what the inventory and
// its screen need about each item (shape, slot kind, sockets), its .wpn and
// its description.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/itm.hpp"
#include "formats/mgr.hpp"
#include "formats/wpn.hpp"
#include "world/inventory.hpp"

namespace harbinger::world {

class ItemCatalog {
public:
    ItemCatalog(Vfs& vfs, const Ini& ini);

    // The template, with its class body where that is decoded (else the
    // header only); nullptr if the id or file is missing.
    const itm::Item* item(uint16_t id);
    const ItemInfo* info(uint16_t id);
    // A socketed item's .wpn (Socketed::word38 in weapons.mgr), or nullptr.
    const wpn::Weapon* weaponOf(uint16_t itemId);
    // The description popup's text for an item and the chips in its sockets.
    std::string description(const Carried& c);
    // An Inventory::Lookup over this catalogue.
    Inventory::Lookup lookup() {
        return [this](uint16_t id) { return info(id); };
    }

private:
    Vfs& vfs_;
    mgr::Index items_, weapons_;
    std::map<uint16_t, std::optional<itm::Item>> itemCache_;
    std::map<uint16_t, std::optional<ItemInfo>> infoCache_;
    std::map<uint16_t, std::optional<wpn::Weapon>> weaponCache_;
};

}  // namespace harbinger::world
