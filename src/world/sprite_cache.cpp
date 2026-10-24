#include "world/sprite_cache.hpp"

#include <algorithm>

namespace harbinger::world {

namespace {

void stack(render::RgbaImage& dst, const std::vector<uint32_t>& src, int w, int h) {
    for (int v = 0; v < std::min(h, dst.height); ++v)
        for (int u = 0; u < std::min(w, dst.width); ++u) {
            const uint32_t p = src[size_t(v) * size_t(w) + size_t(u)];
            const uint32_t a = p >> 24;
            if (!a) continue;
            uint32_t& d = dst.pixels[size_t(v) * size_t(dst.width) + size_t(u)];
            if (a == 255) d = p;
            else if ((d >> 24) < a) d = p;
        }
}

}  // namespace

const anm::Animation* SpriteCache::anim(const std::string& path) {
    auto it = anims_.find(path);
    if (it == anims_.end()) {
        std::unique_ptr<anm::Animation> a;
        if (auto data = vfs_.read(path)) a = std::make_unique<anm::Animation>(anm::parse(*data));
        it = anims_.emplace(path, std::move(a)).first;
    }
    return it->second.get();
}

const gim::Image* SpriteCache::sheet(const std::string& path) {
    auto it = sheets_.find(path);
    if (it == sheets_.end()) {
        std::unique_ptr<gim::Image> img;
        if (auto data = vfs_.read(path)) img = std::make_unique<gim::Image>(gim::decode(*data));
        it = sheets_.emplace(path, std::move(img)).first;
    }
    return it->second.get();
}

const render::RgbaImage* SpriteCache::compose(const std::string& key, const std::vector<const anm::Animation*>& layers,
                                              size_t dir, size_t frame) {
    auto it = composed_.find(key);
    if (it == composed_.end()) {
        render::RgbaImage img;
        for (const anm::Animation* a : layers) {
            if (dir >= a->directions.size() || a->directions[dir].frames.empty()) continue;
            const gim::Image* s = sheet(anm::sheetPath(a->directions[dir].frames[0]));
            if (!s || s->frames.empty()) continue;
            if (img.pixels.empty()) {
                img.width = s->width;
                img.height = s->height;
                img.pixels.assign(size_t(img.width) * size_t(img.height), 0);
            }
            stack(img, render::toRgba(*s, std::min(frame, s->frames.size() - 1), render::KeyMode::Agent), s->width,
                  s->height);
        }
        it = composed_.emplace(key, std::move(img)).first;
    }
    return it->second.pixels.empty() ? nullptr : &it->second;
}

}  // namespace harbinger::world
