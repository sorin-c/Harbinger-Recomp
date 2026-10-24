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

    const auto agents = mgr::parse(readOrThrow(vfs, ini.require("Paths", "Agents")));
    chk::parseAgents(map_, chkData, [&](uint16_t agentId) -> const agt::Template* {
        auto it = agents_.find(agentId);
        if (it == agents_.end()) {
            const std::string* agtPath = agents.find(agentId);
            if (!agtPath) return nullptr;
            it = agents_.emplace(agentId, agt::parse(readOrThrow(vfs, *agtPath))).first;
        }
        return &it->second;
    });
    chk::parseRest(map_, chkData);

    const auto power = startingPower(map_, [&](const chk::GizmoRecord& g) -> const giz::Template& { return gizmoTemplate(g); });
    for (size_t i = 0; i < map_.gizmos.size(); ++i) {
        const chk::GizmoRecord& g = map_.gizmos[i];
        const auto it = power.find(g.instance);
        GizmoRuntime& rt = gizmoRuntime_[g.instance];
        rt.state = startingState(gizmoTemplate(g), g, it == power.end() ? 0 : it->second);
        rt.play = playFor(gizmoTemplate(g), rt.state);
        gizmoIndex_[g.instance] = i;
    }
    for (const auto& g : map_.gizmos) {
        GizmoRuntime& rt = gizmoRuntime_[g.instance];
        rt.mask = gizmoMask(g);
        for (const auto& f : gizmoTemplate(g).frames) rt.maskVaries |= f.mask != gizmoTemplate(g).frames[0].mask;
    }
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

size_t Level::gizmoFrame(const chk::GizmoRecord& g, uint64_t ms) const {
    const GizmoRuntime& rt = gizmoRuntime_.at(g.instance);
    return playFrame(gizmoTemplate(g), rt.play, ms > rt.startMs ? ms - rt.startMs : 0, g.instance);
}

size_t Level::gizmoMask(const chk::GizmoRecord& g) const {
    const giz::Template& t = gizmoTemplate(g);
    if (t.frames.empty()) return 0;
    const size_t f = std::min(gizmoFrame(g, nowMs_), t.frames.size() - 1);
    return t.frames[f].mask < t.masks.size() ? t.frames[f].mask : 0;
}

const chk::GizmoRecord* Level::gizmoByInstance(uint16_t instance) const {
    const auto it = gizmoIndex_.find(instance);
    return it == gizmoIndex_.end() ? nullptr : &map_.gizmos[it->second];
}

Level::MapPoint Level::gizmoUsePoint(const chk::GizmoRecord& g) {
    const giz::Template& t = gizmoTemplate(g);
    if (t.offset[0] < 0 || t.offset[1] < 0 || t.frames.empty()) return {g.x + 0.5, g.y + 0.5};
    // FUN_004b3690: X = (px - 64) / 128, Y = (py - height + (rows + 1) * 32) / 64,
    // then x = X + Y, y = Y - X from the gizmo's cell.
    const render::RgbaImage* img = gizmoImage(t.frames[0]);
    const int rows = t.masks.empty() ? 0 : t.masks[0].rows;
    const double X = (t.offset[0] - 64) / 128.0;
    const double Y = (t.offset[1] - (img ? img->height : 0) + (rows + 1) * 32) / 64.0;
    return {g.x + X + Y, g.y + Y - X};
}

void Level::setGizmoState(const chk::GizmoRecord& g, GizmoRuntime& rt, uint64_t nowMs) {
    const giz::Template& t = gizmoTemplate(g);
    rt.play = playFor(t, rt.state);
    rt.startMs = nowMs;
    const uint64_t length = playLength(t, rt.play);
    rt.endMs = length ? nowMs + length : 0;
}

bool Level::useGizmo(const chk::GizmoRecord& g, uint64_t nowMs,
                     const std::function<bool(const chk::GizmoRecord&)>& occupied) {
    const giz::Template& t = gizmoTemplate(g);
    GizmoRuntime& rt = gizmoRuntime_.at(g.instance);
    if (!activate(t, rt.state)) return false;
    setGizmoState(g, rt, nowMs);
    if (t.cls == giz::Console || t.cls == giz::CompuConsole) {
        // FUN_004bdaf0: every attached gizmo takes its turn; the console
        // goes back to idle when they have settled (GConsolePlug_v0).
        rt.waitingOn.clear();
        for (const auto& [id, list] : map_.attachments) {
            if (id != g.instance) continue;
            for (uint16_t other : list) {
                const chk::GizmoRecord* o = gizmoByInstance(other);
                if (!o) continue;
                GizmoRuntime& ort = gizmoRuntime_.at(other);
                if (!toggle(gizmoTemplate(*o), ort.state, occupied(*o))) continue;
                setGizmoState(*o, ort, nowMs);
                rt.waitingOn.push_back(other);
            }
        }
        // Nothing to wait for: one pass of the activation, then idle (a
        // rebuild choice; the original waits on its attachments).
        if (rt.waitingOn.empty()) {
            const giz::Template& ct = gizmoTemplate(g);
            uint64_t pass = 0;
            for (size_t i = rt.play.first; i <= rt.play.last && i < ct.frames.size(); ++i) pass += ct.frames[i].durationMs;
            rt.endMs = nowMs + pass;
        }
    }
    return true;
}

std::vector<const chk::GizmoRecord*> Level::update(uint64_t nowMs) {
    nowMs_ = nowMs;
    // Settle the moving gizmos whose frames have played.
    for (auto& [instance, rt] : gizmoRuntime_) {
        if (!rt.endMs || nowMs < rt.endMs) continue;
        const chk::GizmoRecord& g = *gizmoByInstance(instance);
        const giz::Template& t = gizmoTemplate(g);
        if ((t.cls == giz::Console || t.cls == giz::CompuConsole) && !rt.waitingOn.empty()) continue;
        if (finish(t, rt.state)) setGizmoState(g, rt, rt.endMs);
        else rt.endMs = 0;
    }
    // Consoles whose attached gizmos have all settled go back to idle.
    for (auto& [instance, rt] : gizmoRuntime_) {
        if (rt.waitingOn.empty()) continue;
        rt.waitingOn.erase(std::remove_if(rt.waitingOn.begin(), rt.waitingOn.end(),
                                          [&](uint16_t o) { return gizmoRuntime_.at(o).endMs == 0; }),
                           rt.waitingOn.end());
        if (rt.waitingOn.empty()) {
            const chk::GizmoRecord& g = *gizmoByInstance(instance);
            if (finish(gizmoTemplate(g), rt.state)) setGizmoState(g, rt, nowMs);
        }
    }
    std::vector<const chk::GizmoRecord*> changed;
    for (const auto& g : map_.gizmos) {
        GizmoRuntime& rt = gizmoRuntime_.at(g.instance);
        if (!rt.maskVaries) continue;
        const size_t mask = gizmoMask(g);
        if (mask != rt.mask) {
            rt.mask = mask;
            changed.push_back(&g);
        }
    }
    return changed;
}

ScreenPoint Level::gizmoFrameTopLeft(const chk::GizmoRecord& g, size_t i) {
    ScreenPoint tl = gizmoTopLeft(g);
    if (i > 0) {  // FUN_004b2bb0 draws the first frame at the origin
        const giz::Frame& f = gizmoTemplate(g).frames[i];
        tl.x += f.x;
        tl.y += f.y;
    }
    return tl;
}

std::vector<size_t> Level::gizmoLayers(const chk::GizmoRecord& g, size_t f) {
    const giz::Template& t = gizmoTemplate(g);
    if (f >= t.frames.size()) return {};
    const render::RgbaImage* first = gizmoImage(t.frames[0]);
    auto covers = [&](size_t i) {
        if (i == 0) return true;
        const giz::Frame& fr = t.frames[i];
        const render::RgbaImage* img = gizmoImage(fr);
        return fr.x == 0 && fr.y == 0 && img && first && img->width == first->width && img->height == first->height;
    };
    size_t from = f;
    while (!covers(from)) --from;
    std::vector<size_t> layers;
    for (size_t i = from; i <= f; ++i) layers.push_back(i);
    return layers;
}

void Level::drawGizmo(render::RgbaImage& dst, const chk::GizmoRecord& g, int originX, int originY, uint64_t ms) {
    const giz::Template& t = gizmoTemplate(g);
    if (!gizmoVisible(g)) return;
    for (const size_t i : gizmoLayers(g, gizmoFrame(g, ms))) {
        const render::RgbaImage* img = gizmoImage(t.frames[i]);
        if (!img) continue;
        const ScreenPoint tl = gizmoFrameTopLeft(g, i);
        const int left = int(std::lround(tl.x)) - originX, top = int(std::lround(tl.y)) - originY;
        for (int v = 0; v < img->height; ++v)
            for (int u = 0; u < img->width; ++u)
                blend(dst, left + u, top + v, img->pixels[size_t(v) * size_t(img->width) + size_t(u)]);
    }
}

const agt::Template& Level::agentTemplate(const chk::AgentRecord& a) const {
    return agents_.at(a.agent);  // parseAgents loaded every template the records name
}

const Level::AgentAnim& Level::agentAnim(const chk::AgentRecord& a) {
    const agt::Template& t = agentTemplate(a);
    const auto idle = t.animsFor(agt::Idle);
    // FUN_00407950 picks a state's animation at random; the rebuild picks by
    // instance so a level looks the same every time.
    const size_t variant = idle.empty() ? 0 : a.instance % idle.size();
    const auto key = std::make_tuple(a.agent, a.facing, variant);
    if (auto it = agentAnims_.find(key); it != agentAnims_.end()) return it->second;
    AgentAnim& out = agentAnims_[key];
    const agt::Anim* chosen = idle.empty() ? (t.anims.empty() ? nullptr : &t.anims[0]) : idle[variant];
    if (!chosen) return out;
    const auto anmData = vfs_.read(chosen->path);
    if (!anmData) return out;
    const anm::Animation anim = anm::parse(*anmData);
    const anm::Direction* dir = &anim.directions[anm::directionFor(a.facing, anim.directions.size())];
    if (dir->frames.empty()) dir = anim.firstDirection();
    if (!dir) return out;
    out.sheet = anm::sheetPath(dir->frames[0]);
    out.frames = anim.directions[0].frames.empty() ? dir->frames.size() : anim.directions[0].frames.size();
    auto& sheet = agentSheets_[out.sheet];
    if (sheet.empty()) {
        if (auto data = vfs_.read(out.sheet)) {
            const auto img = gim::decode(*data);
            for (size_t f = 0; f < img.frames.size(); ++f)
                sheet.push_back({img.width, img.height, render::toRgba(img, f, render::KeyMode::Agent)});
        }
    }
    if (sheet.empty()) return out;
    out.images = &sheet;
    // The hotspot sits near the bottom of the frame in every animation that
    // has one, so it is taken as the ground point (guess); without one, the
    // bottom centre.
    out.anchorX = anim.hotspot ? anim.hotspot->first : sheet[0].width / 2;
    out.anchorY = anim.hotspot ? anim.hotspot->second : sheet[0].height;
    return out;
}

Level::Pose Level::agentPose(const chk::AgentRecord& a, uint64_t ms) {
    const AgentAnim& anim = agentAnim(a);
    Pose pose;
    if (!anim.images || anim.frames == 0) return pose;
    const size_t frame = std::min(size_t(ms / kAgentFrameMs % anim.frames), anim.images->size() - 1);
    pose.key = anim.sheet + "#" + std::to_string(frame);
    pose.image = &(*anim.images)[frame];
    pose.anchorX = anim.anchorX;
    pose.anchorY = anim.anchorY;
    return pose;
}

void Level::drawAgent(render::RgbaImage& dst, const chk::AgentRecord& a, int originX, int originY, uint64_t ms) {
    if (!agentVisible(a)) return;
    const Pose pose = agentPose(a, ms);
    if (!pose.image) return;
    const ScreenPoint p = toScreen(a.x, a.y);
    const int left = int(std::lround(p.x)) - pose.anchorX - originX;
    const int top = int(std::lround(p.y)) - pose.anchorY - originY;
    const render::RgbaImage& img = *pose.image;
    for (int v = 0; v < img.height; ++v)
        for (int u = 0; u < img.width; ++u)
            blend(dst, left + u, top + v, img.pixels[size_t(v) * size_t(img.width) + size_t(u)]);
}

}  // namespace harbinger::world
