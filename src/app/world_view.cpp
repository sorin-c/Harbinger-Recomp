#include "app/world_view.hpp"

#include <algorithm>
#include <cmath>

namespace harbinger::app {

WorldView::WorldView(Assets& assets, SDL_Renderer* ren, uint16_t levelId, world::Character character, int screenW,
                     int screenH)
    : assets_(assets), ren_(ren), level_(std::make_unique<world::Level>(assets.vfs(), assets.ini(), levelId)),
      screenW_(screenW), screenH_(screenH) {
    const auto& m = level_->map();
    // A new game enters at entry point 0, "Default" (FUN_004a05d0 sets
    // +0x136 = 0; FUN_00498250 looks it up).
    chk::EntryPoint start;
    start.x = m.width / 2.0, start.y = m.height / 2.0;
    for (const auto& e : m.entries)
        if (e.id == 0 || e.name == "Default") {
            start = e;
            break;
        }
    walk_ = std::make_unique<world::WalkMap>(*level_);
    player_ = std::make_unique<world::Player>(assets.vfs(), assets.ini(), character, *walk_, start);
    const auto p = world::toScreen(player_->x(), player_->y());
    camX_ = p.x, camY_ = p.y;

    for (const auto& g : m.gizmos)
        if (level_->gizmoVisible(g)) {
            Object o{level_->gizmoDepth(g), &g, nullptr};
            o.minX = o.maxX = g.x, o.minY = o.maxY = g.y + 0.5;
            const giz::Template& t = level_->gizmoTemplate(g);
            if (!t.masks.empty() && !t.frames.empty() && t.frames[0].mask < t.masks.size()) {
                // The box of the half cells the gizmo blocks (mask `a` bit 0,
                // see world/walkmap.cpp), or of all of them if it blocks none:
                // what it covers on the floor that can be walked around.
                const auto& cells = t.masks[t.frames[0].mask].cells;
                const bool anyBlocked = std::any_of(cells.begin(), cells.end(), [](const auto& c) { return c.a & 1; });
                bool first = true;
                for (const auto& c : cells) {
                    if (anyBlocked && !(c.a & 1)) continue;
                    const double x = (2 * g.x + c.dx) / 2.0, y = (2 * g.y + 1 + c.dy) / 2.0;
                    if (first) o.minX = o.maxX = x, o.minY = o.maxY = y, first = false;
                    o.minX = std::min(o.minX, x), o.maxX = std::max(o.maxX, x + 0.5);
                    o.minY = std::min(o.minY, y), o.maxY = std::max(o.maxY, y + 0.5);
                }
            }
            objects_.push_back(o);
        }
    for (const auto& a : m.agents)
        if (level_->agentVisible(a)) objects_.push_back({level_->agentDepth(a), nullptr, &a});
    std::stable_sort(objects_.begin(), objects_.end(),
                     [](const auto& a, const auto& b) { return a.depth < b.depth; });
}

WorldView::~WorldView() {
    for (auto& [key, t] : tiles_) SDL_DestroyTexture(t.tex);
    for (auto& [key, t] : objectTextures_) SDL_DestroyTexture(t.tex);
}

void WorldView::steer(float dx, float dy) {
    // A screen direction to a map one: invert screen = ((x - y) * 64, (x + y) * 32).
    const double mx = dx / 64.0 + dy / 32.0, my = dy / 32.0 - dx / 64.0;
    player_->steer(mx, my);
}

world::WalkMap::Point WorldView::mapPoint(float x, float y) const {
    const double sx = x - screenW_ / 2.0 + camX_, sy = y - screenH_ / 2.0 + camY_;
    return {(sx / 64.0 + sy / 32.0) / 2.0, (sy / 32.0 - sx / 64.0) / 2.0};
}

void WorldView::walkTo(float x, float y) {
    const auto p = mapPoint(x, y);
    player_->walkTo(p.x, p.y);
}

void WorldView::update(uint64_t nowMs) {
    if (!startMs_) startMs_ = nowMs;
    elapsedMs_ = nowMs - startMs_;
    player_->update(elapsedMs_);
    // The camera stays centred on the player (FUN_00498250 and the per-frame
    // view update: the followed agent's screen position minus half the view).
    const auto p = world::toScreen(player_->x(), player_->y());
    camX_ = std::floor(p.x), camY_ = std::floor(p.y);
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
    const auto& frames = level_->gizmoTemplate(g).frames;
    for (const size_t i : level_->gizmoLayers(g, level_->gizmoFrame(g, elapsedMs_))) {
        const auto tl = level_->gizmoFrameTopLeft(g, i);
        drawImage(frames[i].path, level_->gizmoImage(frames[i]), tl.x, tl.y);
    }
}

void WorldView::drawAgent(const chk::AgentRecord& a) const {
    const auto pose = level_->agentPose(a, elapsedMs_);
    if (!pose.image) return;
    const auto p = world::toScreen(a.x, a.y);
    drawImage(pose.key, pose.image, p.x - pose.anchorX, p.y - pose.anchorY);
}

void WorldView::drawPlayer() const {
    const auto pose = player_->pose(elapsedMs_);
    if (!pose.image) return;
    const auto p = world::toScreen(player_->x(), player_->y());
    drawImage(pose.key, pose.image, p.x - pose.anchorX, p.y - pose.anchorY);
}

double WorldView::playerDepth() const {
    // Large gizmos (the umbilical pit, the generator) span many cells, so one
    // depth per gizmo puts a player standing beside them on the wrong side.
    // Against a nearby gizmo's footprint box the player is in front when it
    // is past the box along x or y, behind when it is short of it; otherwise
    // the depths decide. (A rebuild rule: the original draws with Direct3D,
    // not traced yet.)
    const double px = player_->x(), py = player_->y(), own = px + py;
    double after = -1e9, before = 1e9;
    for (const Object& o : objects_) {
        if (!o.gizmo || std::fabs(o.depth - own) > 12) continue;
        const bool front = px >= o.maxX || py >= o.maxY, behind = px <= o.minX || py <= o.minY;
        if (front && !behind) after = std::max(after, o.depth);
        if (behind && !front) before = std::min(before, o.depth);
    }
    double depth = own;
    if (depth <= after) depth = after + 1e-6;
    if (depth >= before && before > after) depth = before - 1e-6;
    return depth;
}

void WorldView::draw() const {
    const auto& m = level_->map();
    // Floors, then walls, edges, gizmos and agents back to front (rebuild order; the
    // original draws through Direct3D with depth, see docs/formats/level.md).
    for (int x = 0; x < m.width; ++x)
        for (int y = 0; y < m.height; ++y) drawLayer(x, y, 0);
    auto drawObject = [&](const Object& o) { o.gizmo ? drawGizmo(*o.gizmo) : drawAgent(*o.agent); };
    // The player sorts with the objects by the same depth (x + y).
    const double playerDepth = this->playerDepth();
    bool playerDrawn = false;
    auto drawUpTo = [&](double depth, size_t& next) {
        for (; next < objects_.size() && objects_[next].depth < depth; ++next) {
            if (!playerDrawn && playerDepth < objects_[next].depth) drawPlayer(), playerDrawn = true;
            drawObject(objects_[next]);
        }
        if (!playerDrawn && playerDepth < depth) drawPlayer(), playerDrawn = true;
    };
    size_t next = 0;
    for (int d = 0; d < m.width + m.height; ++d) {
        for (int x = std::max(0, d - m.height + 1); x < m.width && x <= d; ++x) drawLayer(x, d - x, 1);
        drawUpTo(d + 1, next);
    }
    drawUpTo(1e9, next);
}

}  // namespace harbinger::app
