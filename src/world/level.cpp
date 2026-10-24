#include "world/level.hpp"

#include <algorithm>
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

// Source-over blend of `p` onto pixel (x, y).
void blend(render::RgbaImage& dst, int x, int y, uint32_t p) {
    const uint32_t a = p >> 24;
    if (x < 0 || y < 0 || x >= dst.width || y >= dst.height || !a) return;
    uint32_t& d = dst.pixels[size_t(y) * size_t(dst.width) + size_t(x)];
    if (a == 255) {
        d = p;
        return;
    }
    uint32_t out = 0xFF000000u;
    for (int shift = 0; shift < 24; shift += 8) {
        const uint32_t s = (p >> shift) & 0xFF, t = (d >> shift) & 0xFF;
        out |= ((s * a + t * (255 - a)) / 255) << shift;
    }
    d = out;
}

}  // namespace

Level::Level(Vfs& vfs, const Ini& ini, uint16_t id) : vfs_(vfs) {
    const auto levels = mgr::parse(readOrThrow(vfs, ini.require("Paths", "Levels")));
    const std::string* path = levels.find(id);
    if (!path) throw std::runtime_error("level " + std::to_string(id) + " is not in level.mgr");
    path_ = *path;
    const auto chkData = readOrThrow(vfs, path_);
    map_ = chk::parse(chkData);

    const auto brushes = mgr::parse(readOrThrow(vfs, ini.require("Act" + std::to_string(map_.act), "Brushes")));
    for (const auto& [brushId, brushPath] : brushes.entries)
        brushes_[brushId] = LoadedBrush{brs::parse(readOrThrow(vfs, brushPath))};

    const auto gizmos = mgr::parse(readOrThrow(vfs, ini.require("Act" + std::to_string(map_.act), "Gizmos")));
    chk::parseGizmos(map_, chkData, [&](uint16_t gizmoId) -> const giz::Template* {
        auto it = gizmos_.find(gizmoId);
        if (it == gizmos_.end()) {
            const std::string* gizPath = gizmos.find(gizmoId);
            if (!gizPath) return nullptr;
            it = gizmos_.emplace(gizmoId, giz::parse(readOrThrow(vfs, *gizPath))).first;
        }
        return &it->second;
    });
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

render::RgbaImage Level::cutCentre(const chk::Layer& layer) {
    render::RgbaImage out;
    const render::RgbaImage* img = image(layer);
    if (!img || !img->width || !img->height || layer.tile >> 12 != brs::Centre) return out;
    out.width = kTileW;
    out.height = kTileH;
    out.pixels.assign(size_t(kTileW) * kTileH, 0);
    const brs::Offset o = brs::centreOffset(layer.tile);
    for (int v = 0; v < kTileH; ++v)
        for (int u = 0; u < kTileW; ++u)
            if (inDiamond(u, v))
                out.pixels[size_t(v) * kTileW + size_t(u)] =
                    img->pixels[size_t((o.y + v) % img->height) * size_t(img->width) + size_t((o.x + u) % img->width)];
    return out;
}

void Level::drawLayer(render::RgbaImage& dst, int x, int y, int layer, int originX, int originY) {
    const chk::Cell& cell = map_.at(x, y);
    if (layer >= cell.count) return;
    const chk::Layer& l = cell.layers[layer];
    const render::RgbaImage* img = image(l);
    if (!img || !img->width || !img->height) return;
    const ScreenPoint top = toScreen(x, y);
    const int left = int(top.x) - kTileW / 2 - originX, boxTop = int(top.y) - originY;
    const render::RgbaImage cut = l.tile >> 12 == brs::Centre ? cutCentre(l) : render::RgbaImage{};
    const render::RgbaImage& src = cut.width ? cut : *img;
    const int imgTop = boxTop + kTileH - src.height;
    for (int v = 0; v < src.height; ++v)
        for (int u = 0; u < src.width; ++u)
            put(dst, left + u, imgTop + v, src.pixels[size_t(v) * size_t(src.width) + size_t(u)]);
}

const giz::Template& Level::gizmoTemplate(const chk::GizmoRecord& g) const {
    return gizmos_.at(g.gizmo);  // parseGizmos loaded every template the records name
}

const render::RgbaImage* Level::gizmoImage(const giz::Frame& frame) {
    auto& slot = gizmoImages_[frame.path];
    if (!slot) {
        auto data = vfs_.read(frame.path);
        if (!data) return nullptr;
        const auto img = gim::decode(*data);
        slot = std::make_unique<render::RgbaImage>(
            render::RgbaImage{img.width, img.height, render::toRgba(img, 0, render::KeyMode::Shadow)});
    }
    return slot.get();
}

ScreenPoint Level::gizmoTopLeft(const chk::GizmoRecord& g) {
    const giz::Template& t = gizmoTemplate(g);
    const ScreenPoint p = toScreen(g.x, g.y);
    const render::RgbaImage* img = t.frames.empty() ? nullptr : gizmoImage(t.frames[0]);
    const int rows = t.masks.empty() ? 0 : t.masks[0].rows;
    const int h = img ? img->height : 0;
    return {p.x - kTileW / 2, p.y + (rows + 1) * (kTileH / 2) - h};
}

double Level::gizmoDepth(const chk::GizmoRecord& g) const {
    // The footprint runs from the origin half-cell towards +x and -y, so its
    // nearest half-cell is the one with the largest dx + dy.
    const giz::Template& t = gizmoTemplate(g);
    int front = 0;
    if (!t.masks.empty())
        for (const auto& c : t.masks[0].cells) front = std::max(front, c.dx + c.dy);
    return g.x + g.y + front / 2.0;
}

bool Level::gizmoVisible(const chk::GizmoRecord& g) const {
    const giz::Template& t = gizmoTemplate(g);
    return map_.keeps(g) && !t.frames.empty() && t.cls != giz::UmbilicalShell;
}

void Level::drawGizmo(render::RgbaImage& dst, const chk::GizmoRecord& g, int originX, int originY) {
    const giz::Template& t = gizmoTemplate(g);
    if (!gizmoVisible(g)) return;
    const render::RgbaImage* img = gizmoImage(t.frames[0]);
    if (!img) return;
    const ScreenPoint tl = gizmoTopLeft(g);
    const int left = int(std::lround(tl.x)) - originX, top = int(std::lround(tl.y)) - originY;
    for (int v = 0; v < img->height; ++v)
        for (int u = 0; u < img->width; ++u)
            blend(dst, left + u, top + v, img->pixels[size_t(v) * size_t(img->width) + size_t(u)]);
}

}  // namespace harbinger::world
