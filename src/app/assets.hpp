// Game data shared by every screen: Harbinger.ini, the resource indexes, and
// caches of decoded images, textures, fonts and sounds.
#pragma once

#include <SDL3/SDL.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "audio/pcm.hpp"
#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/fnt.hpp"
#include "formats/gim.hpp"
#include "formats/itm.hpp"
#include "formats/mgr.hpp"

namespace harbinger::app {

struct Texture {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
};

class Assets {
public:
    Assets(SDL_Renderer* ren, const std::string& installDir);
    ~Assets();
    Assets(const Assets&) = delete;
    Assets& operator=(const Assets&) = delete;

    Vfs& vfs() { return vfs_; }
    const Ini& ini() const { return ini_; }
    std::vector<uint8_t> read(const std::string& path);

    const gim::Image& image(const std::string& path);
    // UI art: the green key colour is transparent.
    const Texture& texture(const std::string& path);
    Texture upload(const std::vector<uint32_t>& rgba, int w, int h);

    const fnt::Font& font(uint16_t id);
    itm::Item item(uint16_t id);
    std::string itemImagePath(uint16_t id);
    std::string cursorPath(uint16_t id);
    // sound.mgr id -> decoded PCM, or nullptr if the id is unknown or undecodable.
    const audio::Pcm* sound(uint16_t id);

private:
    static std::string lookup(const mgr::Index& index, uint16_t id, const char* what);

    SDL_Renderer* ren_;
    Vfs vfs_;
    Ini ini_;
    mgr::Index fonts_, items_, itemImages_, cursors_, sounds_;
    std::map<uint16_t, fnt::Font> fontCache_;
    std::map<std::string, gim::Image> images_;
    std::map<std::string, Texture> textures_;
    std::map<uint16_t, std::unique_ptr<audio::Pcm>> soundCache_;
};

}  // namespace harbinger::app
