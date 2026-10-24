// Loads agent animations and their sheets once, and builds the layered
// pictures agents are drawn from (the player's body and equipment, an
// enemy's single layer).
#pragma once

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "engine/vfs.hpp"
#include "formats/anm.hpp"
#include "formats/gim.hpp"
#include "render/sprite.hpp"

namespace harbinger::world {

class SpriteCache {
public:
    explicit SpriteCache(Vfs& vfs) : vfs_(vfs) {}

    // Null if the file is missing.
    const anm::Animation* anim(const std::string& path);
    const gim::Image* sheet(const std::string& path);

    // Frame `frame` of direction `dir` of each layer, stacked bottom first
    // as FUN_004c2710 does (art replaces what is below; a marker pixel never
    // covers art). Every layer's sheet has the first's size and lines up
    // with it. Cached under `key`; null if no layer has the frame.
    const render::RgbaImage* compose(const std::string& key, const std::vector<const anm::Animation*>& layers,
                                     size_t dir, size_t frame);

private:
    Vfs& vfs_;
    std::map<std::string, std::unique_ptr<anm::Animation>> anims_;  // by .anm path
    std::map<std::string, std::unique_ptr<gim::Image>> sheets_;     // by sheet path
    std::map<std::string, render::RgbaImage> composed_;              // by key
};

}  // namespace harbinger::world
