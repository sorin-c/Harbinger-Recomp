#include "app/assets.hpp"

#include <stdexcept>

#include "render/sprite.hpp"

namespace harbinger::app {
namespace {

std::vector<uint8_t> readOrThrow(Vfs& vfs, const std::string& path) {
    auto data = vfs.read(path);
    if (!data) throw std::runtime_error("missing resource: " + path);
    return std::move(*data);
}

}  // namespace

// The engine reads every index path from Harbinger.ini's [Paths] section
// (e.g. FUN_005138c0 looks up "Fonts" there).
Assets::Assets(SDL_Renderer* ren, const std::string& installDir)
    : ren_(ren), vfs_(installDir), ini_(Ini::fromBytes(readOrThrow(vfs_, "Harbinger.ini"))) {
    auto index = [&](const char* key) { return mgr::parse(readOrThrow(vfs_, ini_.require("Paths", key))); };
    fonts_ = index("Fonts");
    items_ = index("Items");
    itemImages_ = index("ItemImages");
    cursors_ = index("Cursors");
    sounds_ = index("Sounds");
}

Assets::~Assets() {
    for (auto& [path, t] : textures_) SDL_DestroyTexture(t.tex);
}

std::vector<uint8_t> Assets::read(const std::string& path) { return readOrThrow(vfs_, path); }

const gim::Image& Assets::image(const std::string& path) {
    auto it = images_.find(path);
    if (it == images_.end()) it = images_.emplace(path, gim::decode(read(path))).first;
    return it->second;
}

const Texture& Assets::texture(const std::string& path) {
    auto it = textures_.find(path);
    if (it != textures_.end()) return it->second;
    const gim::Image& img = image(path);
    return textures_[path] = upload(render::toRgba(img, 0, render::KeyMode::Transparent), img.width, img.height);
}

Texture Assets::upload(const std::vector<uint32_t>& rgba, int w, int h) {
    Texture t{SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h), w, h};
    if (!t.tex) throw std::runtime_error(SDL_GetError());
    SDL_UpdateTexture(t.tex, nullptr, rgba.data(), w * 4);
    SDL_SetTextureBlendMode(t.tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t.tex, SDL_SCALEMODE_NEAREST);
    return t;
}

std::string Assets::lookup(const mgr::Index& index, uint16_t id, const char* what) {
    const std::string* path = index.find(id);
    if (!path) throw std::runtime_error(std::string("unknown ") + what + " id " + std::to_string(id));
    return *path;
}

const fnt::Font& Assets::font(uint16_t id) {
    auto it = fontCache_.find(id);
    if (it != fontCache_.end()) return it->second;
    return fontCache_[id] = fnt::parse(read(lookup(fonts_, id, "font")));
}

itm::Item Assets::item(uint16_t id) { return itm::parse(read(lookup(items_, id, "item"))); }
std::string Assets::itemImagePath(uint16_t id) { return lookup(itemImages_, id, "item image"); }
std::string Assets::cursorPath(uint16_t id) { return lookup(cursors_, id, "cursor"); }

const audio::Pcm* Assets::sound(uint16_t id) {
    auto it = soundCache_.find(id);
    if (it == soundCache_.end()) {
        std::unique_ptr<audio::Pcm> pcm;
        try {
            if (const std::string* path = sounds_.find(id)) pcm = std::make_unique<audio::Pcm>(audio::decodeWav(read(*path)));
        } catch (const std::exception& ex) {
            SDL_Log("sound %u: %s", unsigned(id), ex.what());
        }
        it = soundCache_.emplace(id, std::move(pcm)).first;
    }
    return it->second.get();
}

}  // namespace harbinger::app
