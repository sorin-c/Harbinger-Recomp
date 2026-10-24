#include "world/level.hpp"

#include <cmath>
#include <stdexcept>

#include "formats/gim.hpp"
#include "formats/mgr.hpp"

namespace harbinger::world {

namespace {

std::vector<uint8_t> readOrThrow(Vfs& vfs, const std::string& path) {
    auto data = vfs.read(path);
    if (!data) throw std::runtime_error("missing resource: " + path);
    return std::move(*data);
}

// True when pixel (u, v) of a 128x64 box lies inside the tile diamond.
bool inDiamond(int u, int v) {
    return std::fabs(u + 0.5 - kTileW / 2.0) / (kTileW / 2.0) + std::fabs(v + 0.5 - kTileH / 2.0) / (kTileH / 2.0) <= 1.0;
}

void put(render::RgbaImage& dst, int x, int y, uint32_t p) {
    if (x < 0 || y < 0 || x >= dst.width || y >= dst.height || !(p >> 24)) return;
    dst.pixels[size_t(y) * size_t(dst.width) + size_t(x)] = p;
}

}  // namespace

Level::Level(Vfs& vfs, const Ini& ini, uint16_t id) : vfs_(vfs) {
    const auto levels = mgr::parse(readOrThrow(vfs, ini.require("Paths", "Levels")));
    const std::string* path = levels.find(id);
    if (!path) throw std::runtime_error("level " + std::to_string(id) + " is not in level.mgr");
    path_ = *path;
    map_ = chk::parse(readOrThrow(vfs, path_));

    const auto brushes = mgr::parse(readOrThrow(vfs, ini.require("Act" + std::to_string(map_.act), "Brushes")));
    for (const auto& [brushId, brushPath] : brushes.entries)
        brushes_[brushId] = LoadedBrush{brs::parse(readOrThrow(vfs, brushPath))};
}

const brs::Brush* Level::brush(uint16_t id) const {
    const auto it = brushes_.find(id);
    return it == brushes_.end() ? nullptr : &it->second.brush;
}

const render::RgbaImage* Level::image(const chk::Layer& layer) {
    const brs::Brush* b = brush(layer.brush);
    const brs::Tile* tile = b ? brs::find(*b, layer.tile) : nullptr;
    if (!tile) return nullptr;
    auto& slot = images_[tile->path];
    if (!slot) {
        const auto img = gim::decode(readOrThrow(vfs_, tile->path));
        slot = std::make_unique<render::RgbaImage>(
            render::RgbaImage{img.width, img.height, render::toRgba(img, 0, render::KeyMode::Transparent)});
    }
    return slot.get();
}

void Level::drawLayer(render::RgbaImage& dst, int x, int y, int layer, int originX, int originY) {
    const chk::Cell& cell = map_.at(x, y);
    if (layer >= cell.count) return;
    const chk::Layer& l = cell.layers[layer];
    const render::RgbaImage* img = image(l);
    if (!img || !img->width || !img->height) return;
    const ScreenPoint top = toScreen(x, y);
    const int left = int(top.x) - kTileW / 2 - originX, boxTop = int(top.y) - originY;
    if (l.tile >> 12 == brs::Centre) {
        const brs::Offset o = brs::centreOffset(l.tile);
        for (int v = 0; v < kTileH; ++v)
            for (int u = 0; u < kTileW; ++u)
                if (inDiamond(u, v))
                    put(dst, left + u, boxTop + v,
                        img->pixels[size_t((o.y + v) % img->height) * size_t(img->width) + size_t((o.x + u) % img->width)]);
        return;
    }
    const int imgTop = boxTop + kTileH - img->height;
    for (int v = 0; v < img->height; ++v)
        for (int u = 0; u < img->width; ++u)
            put(dst, left + u, imgTop + v, img->pixels[size_t(v) * size_t(img->width) + size_t(u)]);
}

}  // namespace harbinger::world
