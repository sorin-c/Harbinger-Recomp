#include "app/world_view.hpp"

#include <algorithm>
#include <cmath>

namespace harbinger::app {

namespace {
constexpr double kScrollSpeed = 900.0;  // px per second at full deflection (rebuild choice)
}

WorldView::WorldView(Assets& assets, SDL_Renderer* ren, uint16_t levelId, int screenW, int screenH)
    : assets_(assets), ren_(ren), level_(std::make_unique<world::Level>(assets.vfs(), assets.ini(), levelId)),
      screenW_(screenW), screenH_(screenH) {
    const auto& m = level_->map();
    double ex = m.width / 2.0, ey = m.height / 2.0;
    for (const auto& e : m.entries)
        if (e.name == "Default") ex = e.x, ey = e.y;
    const auto p = world::toScreen(ex, ey);
    camX_ = p.x, camY_ = p.y;

    for (const auto& g : m.gizmos)
        if (level_->gizmoVisible(g)) objects_.push_back({level_->gizmoDepth(g), &g, nullptr});
    for (const auto& a : m.agents)
        if (level_->agentVisible(a)) objects_.push_back({level_->agentDepth(a), nullptr, &a});
    std::stable_sort(objects_.begin(), objects_.end(),
                     [](const auto& a, const auto& b) { return a.depth < b.depth; });
}

WorldView::~WorldView() {
    for (auto& [key, t] : tiles_) SDL_DestroyTexture(t.tex);
    for (auto& [key, t] : objectTextures_) SDL_DestroyTexture(t.tex);
}

void WorldView::update(uint64_t nowMs) {
    const double dt = lastMs_ ? double(nowMs - lastMs_) / 1000.0 : 0.0;
    lastMs_ = nowMs;
    camX_ += scrollX_ * kScrollSpeed * dt;
    camY_ += scrollY_ * kScrollSpeed * dt;
    // Keep the view centre over the map's diamond bounding box.
    const auto& m = level_->map();
    const double minX = world::toScreen(0, m.height).x, maxX = world::toScreen(m.width, 0).x;
    const double maxY = world::toScreen(m.width, m.height).y;
    camX_ = std::clamp(camX_, minX, maxX);
    camY_ = std::clamp(camY_, 0.0, maxY);
}

const Texture* WorldView::tile(const chk::Layer& layer) const {
    const bool centre = layer.tile >> 12 == brs::Centre;
    const auto key = std::make_pair(layer.brush, layer.tile);
    if (auto it = tiles_.find(key); it != tiles_.end()) return it->second.tex ? &it->second : nullptr;
    Texture& t = tiles_[key];
    const render::RgbaImage* img = level_->image(layer);
    if (!img) return nullptr;
    const render::RgbaImage cut = centre ? level_->cutCentre(layer) : render::RgbaImage{};
    const render::RgbaImage& src = centre ? cut : *img;
    t = assets_.upload(src.pixels, src.width, src.height);
    return t.tex ? &t : nullptr;
}

void WorldView::drawLayer(int x, int y, int layer) const {
    const chk::Cell& cell = level_->map().at(x, y);
    if (layer >= cell.count) return;
    const Texture* t = tile(cell.layers[layer]);
    if (!t) return;
    const auto top = world::toScreen(x, y);
    const float left = float(std::floor(top.x - camX_ + screenW_ / 2.0)) - world::kTileW / 2.0f;
    const float bottom = float(std::floor(top.y - camY_ + screenH_ / 2.0)) + world::kTileH;
    if (left > screenW_ || left + float(t->w) < 0 || bottom < 0 || bottom - float(t->h) > screenH_) return;
    const SDL_FRect dst{left, bottom - float(t->h), float(t->w), float(t->h)};
    SDL_RenderTexture(ren_, t->tex, nullptr, &dst);
}

void WorldView::drawImage(const std::string& key, const render::RgbaImage* img, double left, double top) const {
    auto it = objectTextures_.find(key);
    if (it == objectTextures_.end()) {
        Texture t;
        if (img) t = assets_.upload(img->pixels, img->width, img->height);
        it = objectTextures_.emplace(key, t).first;
    }
    const Texture& t = it->second;
    if (!t.tex) return;
    const float x = float(std::floor(left - camX_ + screenW_ / 2.0));
    const float y = float(std::floor(top - camY_ + screenH_ / 2.0));
    if (x > screenW_ || x + float(t.w) < 0 || y > screenH_ || y + float(t.h) < 0) return;
    const SDL_FRect dst{x, y, float(t.w), float(t.h)};
    SDL_RenderTexture(ren_, t.tex, nullptr, &dst);
}

void WorldView::drawGizmo(const chk::GizmoRecord& g) const {
    const giz::Frame& frame = level_->gizmoTemplate(g).frames[0];
    const auto tl = level_->gizmoTopLeft(g);
    drawImage(frame.path, level_->gizmoImage(frame), tl.x, tl.y);
}

void WorldView::drawAgent(const chk::AgentRecord& a) const {
    const auto pose = level_->agentPose(a);
    if (!pose.image) return;
    const auto p = world::toScreen(a.x, a.y);
    drawImage("agent:" + std::to_string(a.agent), pose.image, p.x - pose.anchorX, p.y - pose.anchorY);
}

void WorldView::draw() const {
    const auto& m = level_->map();
    // Floors, then walls, edges, gizmos and agents back to front (rebuild order; the
    // original draws through Direct3D with depth, see docs/formats/level.md).
    for (int x = 0; x < m.width; ++x)
        for (int y = 0; y < m.height; ++y) drawLayer(x, y, 0);
    auto drawObject = [&](const Object& o) { o.gizmo ? drawGizmo(*o.gizmo) : drawAgent(*o.agent); };
    size_t next = 0;
    for (int d = 0; d < m.width + m.height; ++d) {
        for (int x = std::max(0, d - m.height + 1); x < m.width && x <= d; ++x) drawLayer(x, d - x, 1);
        for (; next < objects_.size() && objects_[next].depth < d + 1; ++next) drawObject(objects_[next]);
    }
    for (; next < objects_.size(); ++next) drawObject(objects_[next]);
}

}  // namespace harbinger::app
