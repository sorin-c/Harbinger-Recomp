#include "app/world_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <string>

#include "world/motion.hpp"

namespace harbinger::app {

WorldView::WorldView(Assets& assets, SDL_Renderer* ren, uint16_t levelId, world::Character character, int screenW,
                     int screenH, world::Inventory::Layout inventory, uint16_t entry,
                     std::optional<world::WalkMap::Point> at)
    : assets_(assets), ren_(ren), level_(std::make_unique<world::Level>(assets.vfs(), assets.ini(), levelId)),
      screenW_(screenW), screenH_(screenH), centreX_(screenW / 2.0) {
    const auto& m = level_->map();
    // A new game enters at entry point 0, "Default" (FUN_004a05d0 sets
    // +0x136 = 0; FUN_00498250 looks it up).
    chk::EntryPoint start;
    start.x = m.width / 2.0, start.y = m.height / 2.0;
    for (const auto& e : m.entries)
        if (e.id == entry || (entry == 0 && e.name == "Default")) {
            start = e;
            break;
        }
    if (at) start.x = at->x, start.y = at->y;
    walk_ = std::make_unique<world::WalkMap>(*level_);
    player_ = std::make_unique<world::Player>(assets.vfs(), assets.ini(), character, *walk_, start);
    // The starting items go in as pick-ups do (GHumanPlayerPlug_v108 and
    // the others add them one by one), so they end up equipped.
    catalog_ = std::make_unique<world::ItemCatalog>(assets.vfs(), assets.ini());
    inventory_ = std::make_unique<world::Inventory>(std::move(inventory), catalog_->lookup());
    for (uint16_t id : world::Player::startingItems(character)) inventory_->add({id, {}});
    player_->equip(inventory_->equippedIds());
    equipRev_ = inventory_->equipmentRevision();
    // The original seeds its generator once (FUN_004f9030); the rebuild seeds
    // from the clock so fights differ from game to game.
    rng_.seed(uint32_t(SDL_GetTicksNS()));
    foes_ = std::make_unique<world::Foes>(assets.vfs(), assets.ini(), *level_, *walk_, rng_);
    cutscene_ = std::make_unique<Cutscene>(assets, ren, character, *level_, screenW, screenH);
    loot_ = std::make_unique<world::Loot>(assets.vfs(), assets.ini(), character, *level_, *walk_, rng_);
    bolts_ = std::make_unique<world::Bolts>(assets.vfs(), assets.ini(), *walk_, rng_);
    if (const auto* w = player_->rangedWeapon()) player_->setBoltSpeed(bolts_->speedOf(*w));
    for (size_t i = 0; i < foes_->all().size(); ++i)
        if (const auto& r = foes_->all()[i].ranged) foes_->setBoltSpeed(int(i), bolts_->speedOf(*r));
    const auto p = world::toScreen(player_->x(), player_->y());
    camX_ = p.x, camY_ = p.y;

    for (const auto& g : m.gizmos)
        if (level_->gizmoVisible(g)) {
            Object o{level_->gizmoDepth(g), &g, nullptr};
            o.shape = &level_->gizmoShape(g);
            const auto tl = level_->gizmoTopLeft(g);
            o.left = int(std::floor(tl.x)), o.top = int(std::floor(tl.y));
            objects_.push_back(o);
        }
    for (const auto& a : m.agents)
        if (level_->agentVisible(a)) objects_.push_back({level_->agentDepth(a), nullptr, &a});
    std::stable_sort(objects_.begin(), objects_.end(),
                     [](const auto& a, const auto& b) { return a.depth < b.depth; });

    // Gizmos stay put, so which of two overlapping gizmos hides the other is
    // worked out once.
    behind_.resize(objects_.size());
    for (size_t i = 0; i < objects_.size(); ++i) {
        const Object& p = objects_[i];
        if (!p.gizmo || p.shape->depth.empty()) continue;
        for (size_t j = i + 1; j < objects_.size(); ++j) {
            const Object& q = objects_[j];
            if (!q.gizmo || q.shape->depth.empty()) continue;
            const int c = level_->gizmoInFront(*p.gizmo, *q.gizmo);
            if (c > 0) behind_[i].push_back(j);
            else if (c < 0) behind_[j].push_back(i);
        }
    }
}

WorldView::~WorldView() {
    for (auto& [key, t] : tiles_) SDL_DestroyTexture(t.tex);
    for (auto& [key, t] : objectTextures_) SDL_DestroyTexture(t.tex);
}

bool WorldView::useHealthPack() {
    if (cinematic() || player_->dead() || player_->hitPoints() >= player_->maxHitPoints()) return false;
    if (!inventory_->takeHealthPack()) return false;
    // How much a pack heals is not traced (the use goes through the item's
    // own vtable, FUN_004d5a50, and the item files hold no amount): half the
    // maximum is guessed.
    player_->heal(player_->maxHitPoints() / 2);
    return true;
}

void WorldView::steer(float dx, float dy) {
    if (cinematic()) return;
    if (dx != 0 || dy != 0) pendingUse_ = nullptr, pendingPickup_ = 0;
    // A screen direction to a map one: invert screen = ((x - y) * 64, (x + y) * 32).
    const double mx = dx / 64.0 + dy / 32.0, my = dy / 32.0 - dx / 64.0;
    player_->steer(mx, my);
}

world::WalkMap::Point WorldView::mapPoint(float x, float y) const {
    const double sx = x - centreX_ + camX_, sy = y - screenH_ / 2.0 + camY_;
    return {(sx / 64.0 + sy / 32.0) / 2.0, (sy / 32.0 - sx / 64.0) / 2.0};
}

void WorldView::walkTo(float x, float y) {
    if (cinematic()) return;
    pendingUse_ = nullptr, pendingPickup_ = 0;
    const auto p = mapPoint(x, y);
    player_->walkTo(p.x, p.y);
}

int WorldView::foeAt(float x, float y) const {
    // An enemy's picture stands up from its feet: take the click as on it
    // when it falls in a box around the feet's screen point (a rebuild
    // choice, roughly a body's size).
    const double sx = x - centreX_ + camX_, sy = y - screenH_ / 2.0 + camY_;
    int best = -1;
    double bestD = 1e9;
    for (size_t i = 0; i < foes_->all().size(); ++i) {
        const auto& f = foes_->all()[i];
        if (f.dead) continue;
        const auto p = world::toScreen(f.x, f.y);
        const double dx = sx - p.x, dy = sy - p.y;
        if (std::fabs(dx) > 28 || dy < -80 || dy > 16) continue;
        const double d = std::fabs(dx) + std::fabs(dy + 32);
        if (d < bestD) bestD = d, best = int(i);
    }
    return best;
}

bool WorldView::attackAt(float x, float y) {
    if (cinematic()) return false;
    const int best = foeAt(x, y);
    if (best < 0) return false;
    const auto& f = foes_->all()[size_t(best)];
    pendingUse_ = nullptr, pendingPickup_ = 0;
    player_->attack(best, f.x, f.y);
    return true;
}

bool WorldView::shootAt(float x, float y) {
    if (cinematic()) return false;
    const int best = foeAt(x, y);
    if (best < 0) return false;
    const auto& f = foes_->all()[size_t(best)];
    pendingUse_ = nullptr, pendingPickup_ = 0;
    player_->shoot(best, f.x, f.y);
    return true;
}

bool WorldView::shootNearest() {
    if (cinematic()) return false;
    const int id = foes_->nearest(player_->x(), player_->y(), world::Player::kShootRange, player_->facing());
    if (id < 0) return false;
    const auto& f = foes_->all()[size_t(id)];
    pendingUse_ = nullptr, pendingPickup_ = 0;
    player_->shoot(id, f.x, f.y);
    return true;
}

void WorldView::playSound(uint16_t id) {
    if (!sound_ || !sound_->enabled() || id == 0) return;
    // One voice per sound id (a rebuild choice), apart from the scripts'.
    if (const audio::Pcm* pcm = assets_.sound(id)) sound_->play(0x30000 + id, *pcm);
}

bool WorldView::attackNearest() {
    if (cinematic()) return false;
    const int id = foes_->nearest(player_->x(), player_->y(), 4.0, player_->facing());
    if (id < 0) return false;
    const auto& f = foes_->all()[size_t(id)];
    pendingUse_ = nullptr, pendingPickup_ = 0;
    player_->attack(id, f.x, f.y);
    return true;
}

const chk::GizmoRecord* WorldView::gizmoAt(float x, float y) const {
    // The front-most usable gizmo with a drawn pixel under the pointer.
    const double sx = x - centreX_ + camX_, sy = y - screenH_ / 2.0 + camY_;
    for (auto k = order_.rbegin(); k != order_.rend(); ++k) {
        if (*k >= objects_.size()) continue;
        const Object& o = objects_[*k];
        if (!o.gizmo || !world::usable(level_->gizmoTemplate(*o.gizmo))) continue;
        const chk::GizmoRecord& g = *o.gizmo;
        for (const size_t i : level_->gizmoLayers(g, level_->gizmoFrame(g, elapsedMs_))) {
            const render::RgbaImage* img = level_->gizmoFrameImage(g, i);
            if (!img) continue;
            const auto tl = level_->gizmoFrameTopLeft(g, i);
            const int u = int(std::floor(sx - std::floor(tl.x))), v = int(std::floor(sy - std::floor(tl.y)));
            if (u < 0 || v < 0 || u >= img->width || v >= img->height) continue;
            if ((img->pixels[size_t(v) * size_t(img->width) + size_t(u)] >> 24) > 0x40) return &g;
        }
    }
    return nullptr;
}

void WorldView::approach(const chk::GizmoRecord& g) {
    // The use point is often on the gizmo itself (a console's panel): walk
    // to the passable half cell nearest to it (a rebuild choice; the
    // original's GActivateAction uses the gizmo once the agent stands in
    // the point's cell).
    const auto p = level_->gizmoUsePoint(g);
    const int px = int(std::floor(p.x * 2)), py = int(std::floor(p.y * 2));
    double best = 1e9;
    world::WalkMap::Point to{p.x, p.y};
    for (int dx = -6; dx <= 6; ++dx)
        for (int dy = -6; dy <= 6; ++dy) {
            const int sx = px + dx, sy = py + dy;
            if (!walk_->passable(sx, sy, player_->limit())) continue;
            const double cx = (sx + 0.5) / 2.0, cy = (sy + 0.5) / 2.0;
            const double d = (cx - p.x) * (cx - p.x) + (cy - p.y) * (cy - p.y);
            if (d < best) best = d, to = {cx, cy};
        }
    player_->walkTo(to.x, to.y);
    pendingUse_ = &g;
    pendingPickup_ = 0;
}

void WorldView::approachItem(const world::Loot::GroundItem& g) {
    player_->walkTo(g.x, g.y);
    pendingPickup_ = g.id;
    pendingUse_ = nullptr;
}

const Texture* WorldView::groundItemPicture(const world::Loot::GroundItem& g, SDL_FRect& src, SDL_FRect& at) const {
    // The item's image (itemimg.mgr) holds frameCount pictures stacked
    // vertically; the rebuild lays the first one on the ground, centred on
    // the item's point (how the original draws a dropped item is not read).
    if (!g.item) return nullptr;
    std::string path;
    try {
        path = assets_.itemImagePath(g.item->image);
    } catch (const std::exception&) {
        return nullptr;
    }
    const Texture& t = assets_.itemTexture(path);
    if (!t.tex) return nullptr;
    const int frames = std::max<int>(1, g.item->frameCount);
    src = {0, 0, float(t.w), float(t.h / frames)};
    const auto p = world::toScreen(g.x, g.y);
    at = {float(std::floor(p.x - src.w / 2)), float(std::floor(p.y - src.h / 2)), src.w, src.h};
    return &t;
}

const world::Loot::GroundItem* WorldView::itemAt(float x, float y) const {
    const double sx = x - centreX_ + camX_, sy = y - screenH_ / 2.0 + camY_;
    for (auto it = loot_->ground().rbegin(); it != loot_->ground().rend(); ++it) {
        if (!loot_->landed(*it, elapsedMs_)) continue;
        SDL_FRect src, at;
        if (!groundItemPicture(*it, src, at)) continue;
        if (sx >= at.x && sx < at.x + at.w && sy >= at.y && sy < at.y + at.h) return &*it;
    }
    return nullptr;
}

bool WorldView::useAt(float x, float y) {
    if (cinematic()) return false;
    if (const auto* item = itemAt(x, y)) {
        approachItem(*item);
        return true;
    }
    const chk::GizmoRecord* g = gizmoAt(x, y);
    if (!g) return false;
    approach(*g);
    return true;
}

bool WorldView::useNearest() {
    if (cinematic()) return false;
    if (const auto* item = loot_->nearest(player_->x(), player_->y(), kUseRange, elapsedMs_)) {
        approachItem(*item);
        return true;
    }
    const chk::GizmoRecord* best = nullptr;
    double bestD = kUseRange * kUseRange;
    for (const auto& o : objects_) {
        if (!o.gizmo || !world::usable(level_->gizmoTemplate(*o.gizmo))) continue;
        const auto p = level_->gizmoUsePoint(*o.gizmo);
        const double d = (p.x - player_->x()) * (p.x - player_->x()) + (p.y - player_->y()) * (p.y - player_->y());
        if (d <= bestD) bestD = d, best = o.gizmo;
    }
    if (!best) return false;
    approach(*best);
    return true;
}

bool WorldView::occupied(const chk::GizmoRecord& g) const {
    // FUN_004a01e0: an agent whose cell the gizmo covers keeps it from
    // shutting. The cells of all its masks, so a door that is about to shut
    // counts its shut footprint.
    const giz::Template& t = level_->gizmoTemplate(g);
    auto covers = [&](double x, double y) {
        const int cx = int(std::floor(x)), cy = int(std::floor(y));
        for (const auto& m : t.masks)
            for (const auto& c : m.cells) {
                const int sx = 2 * g.x + c.dx, sy = 2 * g.y + 1 + c.dy;
                if (c.kind != 0xd && sx >> 1 == cx && sy >> 1 == cy) return true;
            }
        return false;
    };
    if (covers(player_->x(), player_->y())) return true;
    for (const auto& a : level_->map().agents) {
        const auto at = foes_->position(a);
        if (level_->agentVisible(a) && covers(at.x, at.y)) return true;
    }
    return false;
}

void WorldView::update(uint64_t nowMs) {
    if (!startMs_) startMs_ = nowMs;
    elapsedMs_ = nowMs - startMs_;
    // Gizmos first: a door that has just opened can be walked through.
    for (const chk::GizmoRecord* g : level_->update(elapsedMs_)) walk_->repaint(*level_, *g);
    loot_->update(elapsedMs_);
    if (const auto* f = foes_->find(player_->target())) {
        // A walking foe's velocity and the end of its walk, for leading it.
        double vx = 0, vy = 0;
        std::optional<world::WalkMap::Point> dest;
        if (f->state == agt::Move && !f->path.empty()) {
            vx = world::facingCos(f->facing) * f->speed, vy = world::facingSin(f->facing) * f->speed;
            dest = f->path.back();
        }
        player_->trackTarget(f->x, f->y, !f->dead, vx, vy, dest);
    }
    const double x0 = player_->x(), y0 = player_->y();
    const bool log = std::getenv("HARBINGER_COMBAT_LOG") != nullptr;
    if (const auto blow = player_->update(elapsedMs_)) {
        const auto hit = foes_->blow(blow->target, player_->x(), player_->y(), blow->x, blow->y,
                                     player_->meleeRating(), player_->weapon(), elapsedMs_);
        if (hit && log)
            std::fprintf(stderr, "blow on %d: roll %d %s, %d damage, %d left\n", blow->target, hit->roll,
                         hit->hit ? (hit->critical ? "critical" : "hit") : "miss", hit->damage,
                         foes_->find(blow->target)->hitPoints);
    }
    if (const auto shot = player_->takeShot()) {
        if (bolts_->fire(world::Bolts::kPlayer, shot->x, shot->y, shot->z, shot->heading, *player_->rangedWeapon(),
                         player_->rangedRating(), elapsedMs_))
            playSound(player_->rangedWeapon()->ranged->sound);
    }
    // Bolts in flight: the player's land on live foes, the foes' on the
    // player.
    {
        std::vector<world::Defender> defenders;
        const world::Defender& playerDefender = player_->defender();
        const auto impacts = bolts_->update(elapsedMs_, [&](int shooter) {
            std::vector<world::Bolts::Body> out;
            if (shooter == world::Bolts::kPlayer) {
                defenders.clear();
                defenders.reserve(foes_->all().size());
                for (size_t i = 0; i < foes_->all().size(); ++i) {
                    const auto& f = foes_->all()[i];
                    defenders.push_back(foes_->defender(int(i)));
                    if (f.dead) continue;
                    out.push_back({int(i), f.x, f.y, level_->agentTemplate(*f.record).d20, &defenders.back()});
                }
            } else if (!player_->dead()) {
                out.push_back({world::Bolts::kPlayer, player_->x(), player_->y(), player_->radius(), &playerDefender});
            }
            return out;
        });
        for (const auto& im : impacts) {
            playSound(im.sound);
            if (im.target == world::Bolts::kPlayer) {
                if (im.strike.hit) player_->takeDamage(im.strike.damage, elapsedMs_);
            } else {
                foes_->hurt(im.target, im.strike, elapsedMs_);
            }
            if (log)
                std::fprintf(stderr, "bolt from %d on %d: roll %d %s, %d damage\n", im.shooter, im.target,
                             im.strike.roll, im.strike.hit ? "hit" : "miss", im.strike.damage);
        }
    }
    // The enemies think and move; their blows land on the player. They do
    // not act during a script's cinematic (a rebuild choice).
    if (!cinematic()) {
        world::Quarry q;
        q.x = player_->x(), q.y = player_->y();
        q.radius = player_->radius();
        q.visibility = player_->visibility();
        q.alive = !player_->dead();
        q.defender = &player_->defender();
        if (player_->state() == agt::Move) {
            const double step = player_->speed();
            q.vx = world::facingCos(player_->facing()) * step, q.vy = world::facingSin(player_->facing()) * step;
            if (!player_->path().empty()) q.dest = player_->path().back();
        }
        for (const auto& h : foes_->update(elapsedMs_, q)) {
            if (h.strike.hit) player_->takeDamage(h.strike.damage, elapsedMs_);
            if (log)
                std::fprintf(stderr, "enemy %d blow: roll %d %s, %d damage, player %.0f left\n", h.foe, h.strike.roll,
                             h.strike.hit ? "hit" : "miss", h.strike.damage, player_->hitPoints());
        }
        for (const auto& s : foes_->takeShots()) {
            const auto& f = foes_->all()[size_t(s.foe)];
            if (bolts_->fire(s.foe, s.x, s.y, s.z, s.heading, *f.ranged, f.rating, elapsedMs_))
                playSound(f.ranged->ranged->sound);
            if (log) std::fprintf(stderr, "enemy %d shoots, energy %.0f\n", s.foe, f.energy);
        }
    }
    if (pendingUse_ && player_->path().empty()) {
        const auto p = level_->gizmoUsePoint(*pendingUse_);
        const double d = std::hypot(p.x - player_->x(), p.y - player_->y());
        if (d <= kUseReach)
            level_->useGizmo(*pendingUse_, elapsedMs_, [&](const chk::GizmoRecord& o) { return occupied(o); });
        pendingUse_ = nullptr;
    }
    if (pendingPickup_ && player_->path().empty()) {
        // Picked up on arrival (a rebuild stand-in for GPickUpAction), if
        // the inventory has room; otherwise it stays on the ground (the
        // original says "Unable to fit item in current state").
        if (const auto* g = loot_->find(pendingPickup_))
            if (std::hypot(g->x - player_->x(), g->y - player_->y()) <= kUseReach) {
                world::Inventory trial = *inventory_;
                if (trial.add({g->drop.item, {}}, g->drop.amount) != world::Inventory::Added::NoRoom)
                    if (const auto d = loot_->take(pendingPickup_)) inventory_->add({d->item, {}}, d->amount);
            }
        pendingPickup_ = 0;
    }
    if (inventory_->equipmentRevision() != equipRev_) {
        equipRev_ = inventory_->equipmentRevision();
        player_->equip(inventory_->equippedIds());
        if (const auto* w = player_->rangedWeapon()) player_->setBoltSpeed(bolts_->speedOf(*w));
    }
    // The level's event triggers, and the script one of them started.
    cutscene_->update(elapsedMs_, x0, y0, player_->x(), player_->y(),
                      [&](const chk::GizmoRecord& o) { return occupied(o); });
    if (cinematic() && !wasCinematic_) {
        // Rebuild choice: the player stops where it stands for the cutscene.
        pendingUse_ = nullptr;
        player_->steer(0, 0);
        player_->walkTo(player_->x(), player_->y());
    }
    wasCinematic_ = cinematic();
    // The camera stays centred on the player (FUN_00498250 and the per-frame
    // view update: the followed agent's screen position minus half the view),
    // unless a script has moved it. A shake moves the whole view.
    const auto cam = cutscene_->camera();
    const auto p = cam ? world::toScreen(cam->x, cam->y) : world::toScreen(player_->x(), player_->y());
    camX_ = std::floor(p.x) + cutscene_->shakeX(), camY_ = std::floor(p.y) + cutscene_->shakeY();
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
    const float left = float(std::floor(top.x - camX_ + centreX_)) - world::kTileW / 2.0f;
    const float bottom = float(std::floor(top.y - camY_ + screenH_ / 2.0)) + world::kTileH;
    if (left > screenW_ || left + float(t->w) < 0 || bottom < 0 || bottom - float(t->h) > screenH_) return;
    const SDL_FRect dst{left, bottom - float(t->h), float(t->w), float(t->h)};
    SDL_RenderTexture(ren_, t->tex, nullptr, &dst);
}

void WorldView::drawImage(const std::string& key, const render::RgbaImage* img, double left, double top,
                          bool lit) const {
    auto it = objectTextures_.find(key);
    if (it == objectTextures_.end()) {
        Texture t;
        if (img) t = assets_.upload(img->pixels, img->width, img->height);
        it = objectTextures_.emplace(key, t).first;
    }
    const Texture& t = it->second;
    if (!t.tex) return;
    const float x = float(std::floor(left - camX_ + centreX_));
    const float y = float(std::floor(top - camY_ + screenH_ / 2.0));
    if (x > screenW_ || x + float(t.w) < 0 || y > screenH_ || y + float(t.h) < 0) return;
    const SDL_FRect dst{x, y, float(t.w), float(t.h)};
    SDL_RenderTexture(ren_, t.tex, nullptr, &dst);
    if (lit) {
        // A script's Highlight: how the original lights the gizmo up is not
        // read yet; the rebuild draws it a second time, added on.
        SDL_SetTextureBlendMode(t.tex, SDL_BLENDMODE_ADD);
        SDL_RenderTexture(ren_, t.tex, nullptr, &dst);
        SDL_SetTextureBlendMode(t.tex, SDL_BLENDMODE_BLEND);
    }
}

void WorldView::drawGizmo(const chk::GizmoRecord& g) const {
    const auto& frames = level_->gizmoTemplate(g).frames;
    for (const size_t i : level_->gizmoLayers(g, level_->gizmoFrame(g, elapsedMs_))) {
        const auto tl = level_->gizmoFrameTopLeft(g, i);
        drawImage(std::to_string(g.gizmo) + ":" + std::to_string(i) + ":" + frames[i].path,
                  level_->gizmoFrameImage(g, i), tl.x, tl.y, cutscene_->highlighted(g.instance));
    }
}

void WorldView::drawAgent(const chk::AgentRecord& a) const {
    const auto pose = foes_->pose(a, elapsedMs_);
    if (!pose.image) return;
    const auto at = foes_->position(a);
    const auto p = world::toScreen(at.x, at.y);
    drawImage(pose.key, pose.image, p.x - pose.anchorX, p.y - pose.anchorY);
}

void WorldView::drawPlayer() const {
    const auto pose = player_->pose(elapsedMs_);
    if (!pose.image) return;
    const auto p = world::toScreen(player_->x(), player_->y());
    drawImage(pose.key, pose.image, p.x - pose.anchorX, p.y - pose.anchorY);
}

void WorldView::drawGroundItems() const {
    for (const auto& g : loot_->ground()) {
        if (!loot_->landed(g, elapsedMs_)) continue;
        SDL_FRect src, at;
        const Texture* t = groundItemPicture(g, src, at);
        if (!t) continue;
        at.x = float(std::floor(at.x - camX_ + centreX_)), at.y = float(std::floor(at.y - camY_ + screenH_ / 2.0));
        SDL_RenderTexture(ren_, t->tex, &src, &at);
    }
}

void WorldView::drawTargetBar() const {
    // A small hit-point bar over the enemy being fought (a rebuild stand-in
    // for the HUD's enemy bar, GCtrlEnemyHealthBar).
    const auto* f = foes_->find(player_->target());
    if (!f || f->dead || f->maxHitPoints <= 0) return;
    const auto p = world::toScreen(f->x, f->y);
    const float left = float(std::floor(p.x - camX_ + centreX_)) - 20, top = float(std::floor(p.y - camY_ + screenH_ / 2.0)) - 96;
    const SDL_FRect back{left - 1, top - 1, 42, 6};
    SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
    SDL_RenderFillRect(ren_, &back);
    const SDL_FRect bar{left, top, 40.0f * float(f->hitPoints) / float(f->maxHitPoints), 4};
    SDL_SetRenderDrawColor(ren_, 200, 24, 24, 255);
    SDL_RenderFillRect(ren_, &bar);
}

void WorldView::drawBolts() const {
    constexpr double kPi = 3.14159265358979323846;
    // Each bolt is its billboard's picture (GBillboardDesc), the frame
    // running with time, turned to its direction of flight on screen and
    // lifted by its height. Drawn over the map (a rebuild choice; the
    // original draws particles with the depth buffer). The size is the
    // description's first double (likely) and the frame time 100 ms times
    // its +0x20 (guessed). They are added to what is under them: the
    // particle pass (FUN_0049d180) switches to blending source alpha onto
    // one (FUN_00476900: D3D render states 0x13 = 5, 0x14 = 2) for batches
    // whose flag +0xe is set (likely: which descriptors set it is not
    // traced; the bolts' images have black backgrounds that need it).
    for (const auto& t : bolts_->all()) {
        if (!t.look || !t.look->billboard) continue;
        const emt::Billboard& b = *t.look->billboard;
        const Texture& tex = assets_.itemTexture(b.image);
        if (!tex.tex) continue;
        const int fw = b.frameW > 0 ? b.frameW : tex.w, fh = b.frameH > 0 ? b.frameH : tex.h;
        const int perRow = std::max(1, tex.w / fw);
        const int frames = std::max(1, std::min<int>(b.columns * b.rows, perRow * std::max(1, tex.h / fh)));
        const double frameMs = 100.0 * (b.frameTime > 0 ? b.frameTime : 1.0);
        const int k = int(double(elapsedMs_ - std::min(elapsedMs_, t.born)) / frameMs) % frames;
        const SDL_FRect src{float(k % perRow * fw), float(k / perRow * fh), float(fw), float(fh)};
        const double scale = b.sizes[0] > 0 ? b.sizes[0] : 1.0;
        const auto p = world::toScreen(t.x, t.y);
        const float cx = float(p.x - camX_ + centreX_), cy = float(p.y - camY_ + screenH_ / 2.0 - t.z * 64.0);
        const float w = float(fw * scale), h = float(fh * scale);
        const SDL_FRect at{cx - w / 2, cy - h / 2, w, h};
        const double angle = std::atan2((t.vx + t.vy) * 32.0, (t.vx - t.vy) * 64.0) * 180.0 / kPi;
        SDL_SetTextureBlendMode(tex.tex, SDL_BLENDMODE_ADD);
        SDL_RenderTextureRotated(ren_, tex.tex, &src, &at, angle, nullptr, SDL_FLIP_NONE);
        SDL_SetTextureBlendMode(tex.tex, SDL_BLENDMODE_BLEND);
    }
}

void WorldView::drawHealth() const {
    // The hit points and energy are on the HUD (app/hud); this keeps the
    // death notice.
    if (gameOver()) {
        const char* msg = "You have died. Press a button or click to try again.";
        SDL_RenderDebugText(ren_, float(screenW_) / 2 - 8.0f * float(std::strlen(msg)) / 2, float(screenH_) / 2, msg);
    }
}

WorldView::Sprite WorldView::agentSprite(const chk::AgentRecord* a) const {
    auto place = [](const auto& pose, world::ScreenPoint p) {
        return Sprite{pose.image, int(std::floor(p.x - pose.anchorX)), int(std::floor(p.y - pose.anchorY))};
    };
    if (a) {
        const auto at = foes_->position(*a);
        return place(foes_->pose(*a, elapsedMs_), world::toScreen(at.x, at.y));
    }
    return place(player_->pose(elapsedMs_), world::toScreen(player_->x(), player_->y()));
}

int WorldView::compare(const Object& o, const Sprite& s, double depth) const {
    // An agent's picture stands upright at its map position, so all of it
    // is at the depth of its feet (Level::DepthMap compares map x + y).
    const world::Level::DepthMap& m = *o.shape;
    const int x0 = std::max(o.left, s.left), x1 = std::min(o.left + m.width, s.left + s.image->width);
    const int y0 = std::max(o.top, s.top), y1 = std::min(o.top + m.height, s.top + s.image->height);
    if (x0 >= x1 || y0 >= y1) return 0;
    const double base = o.gizmo->x + o.gizmo->y;
    long front = 0, back = 0;
    for (int y = y0; y < y1; y += 2)
        for (int x = x0; x < x1; x += 2) {
            if ((s.image->pixels[size_t(y - s.top) * size_t(s.image->width) + size_t(x - s.left)] >> 24) < 0x40)
                continue;
            const float d = m.at(x - o.left, y - o.top);
            if (std::isnan(d)) continue;
            if (base + d > depth + 1e-3) ++front;
            else if (base + d < depth - 1e-3) ++back;
        }
    return front > back ? 1 : back > front ? -1 : 0;
}

void WorldView::draw() const {
    const auto& m = level_->map();
    // Floors first. Then walls by diagonal (x + y), back to front, and the
    // gizmos and agents among them (rebuild order; the original draws
    // through Direct3D with a depth buffer, see docs/formats/level.md).
    for (int x = 0; x < m.width; ++x)
        for (int y = 0; y < m.height; ++y) drawLayer(x, y, 0);
    drawGroundItems();  // they lie on the floor, under the walls and everything standing

    // The order of the pictures: a picture that another hides where they
    // overlap draws first; otherwise the smaller depth key first. Node n =
    // objects_.size() is the player.
    const size_t n = objects_.size();
    std::vector<std::vector<size_t>> edges(n + 1);  // edges[i]: drawn after i
    std::vector<int> waiting(n + 1, 0);
    auto edge = [&](size_t first, size_t then) { edges[first].push_back(then), ++waiting[then]; };
    for (size_t i = 0; i < n; ++i)
        for (size_t j : behind_[i]) edge(j, i);
    std::vector<size_t> gizmos;
    for (size_t i = 0; i < n; ++i)
        if (objects_[i].gizmo && !objects_[i].shape->depth.empty()) gizmos.push_back(i);
    auto placeAgent = [&](size_t node, const chk::AgentRecord* a, double depth) {
        const Sprite s = agentSprite(a);
        if (!s.image) return;
        for (size_t gi : gizmos) {
            const int c = compare(objects_[gi], s, depth);
            if (c > 0) edge(node, gi);
            else if (c < 0) edge(gi, node);
        }
    };
    // Agents move, so their depth (map x + y of the feet) is taken now.
    std::vector<double> depth(n + 1);
    for (size_t i = 0; i < n; ++i) {
        depth[i] = objects_[i].depth;
        if (objects_[i].agent) {
            const auto at = foes_->position(*objects_[i].agent);
            depth[i] = at.x + at.y;
        }
    }
    depth[n] = player_->x() + player_->y();
    for (size_t i = 0; i < n; ++i)
        if (objects_[i].agent) placeAgent(i, objects_[i].agent, depth[i]);
    placeAgent(n, nullptr, depth[n]);

    auto depthOf = [&](size_t i) { return depth[i]; };
    auto later = [&](size_t a, size_t b) { return depthOf(a) > depthOf(b); };
    std::priority_queue<size_t, std::vector<size_t>, decltype(later)> ready(later);
    for (size_t i = 0; i <= n; ++i)
        if (!waiting[i]) ready.push(i);
    std::vector<bool> done(n + 1, false);
    order_.clear();
    while (order_.size() <= n) {
        size_t i = n + 1;
        while (!ready.empty() && done[ready.top()]) ready.pop();
        if (!ready.empty()) {
            i = ready.top();
            ready.pop();
        } else {
            // A cycle (three pictures each hiding the next): take the
            // nearest-to-the-back one left.
            for (size_t k = 0; k <= n; ++k)
                if (!done[k] && (i > n || depthOf(k) < depthOf(i))) i = k;
        }
        done[i] = true;
        order_.push_back(i);
        for (size_t j : edges[i])
            if (--waiting[j] == 0 && !done[j]) ready.push(j);
    }

    int diagonal = 0;
    auto wallsUpTo = [&](double depth) {
        for (; diagonal < m.width + m.height && diagonal <= depth; ++diagonal)
            for (int x = std::max(0, diagonal - m.height + 1); x < m.width && x <= diagonal; ++x)
                drawLayer(x, diagonal - x, 1);
    };
    for (size_t i : order_) {
        wallsUpTo(depthOf(i));
        if (i == n) drawPlayer();
        else if (objects_[i].gizmo) drawGizmo(*objects_[i].gizmo);
        else drawAgent(*objects_[i].agent);
    }
    wallsUpTo(1e9);
    drawBolts();
    drawTargetBar();
    drawHealth();
    cutscene_->draw();
}

}  // namespace harbinger::app
