#include "world/spawners.hpp"

#include <algorithm>

#include "world/gizmo_state.hpp"

namespace harbinger::world {

namespace {

bool watched(const giz::Template& t) { return t.cls == giz::Spawn || t.cls == giz::Splitter; }

size_t firstTagged(const giz::Template& t, uint16_t tag, size_t fallback) {
    for (size_t i = 0; i < t.frames.size(); ++i)
        if (t.frames[i].code == tag) return i;
    return fallback;
}

}  // namespace

std::optional<SpawnBinding> spawnBinding(const chk::AgentRecord& a) {
    if (a.extra == 0) return std::nullopt;
    return SpawnBinding{uint16_t(a.extra & 0xfff), uint8_t(a.extra >> 12)};
}

Level::MapPoint spawnPoint(Level& level, const chk::GizmoRecord& g, size_t slot) {
    const giz::Template& t = level.gizmoTemplate(g);
    if (slot >= t.spawns.size() || t.frames.empty()) return {g.x + 0.5, g.y + 0.5};
    // FUN_004b3690, as for use points (Level::gizmoUsePoint).
    const render::RgbaImage* img = level.gizmoImage(t.frames[0]);
    const int rows = t.masks.empty() ? 0 : t.masks[0].rows;
    const double X = (t.spawns[slot].a - 64) / 128.0;
    const double Y = (t.spawns[slot].b - (img ? img->height : 0) + (rows + 1) * 32) / 64.0;
    return {g.x + X + Y, g.y + Y - X};
}

Wake wakeFor(uint16_t cls) {
    switch (cls) {
    case 3:   // GVantirPlug_v1: policy 9, VantirAwake
    case 4:   // GVantirBossPlug_v1: policy 10, VantirBossAwake
    case 8:   // GScintBossPlug_v1: its mind's slot 12 (likely its awake policy)
    case 10:  // GCimBossPlug (Khapra): GBowelHorsePlug_v1, policy 9
    case 12:  // GBowelHorsePlug_v1: policy 9
    case 13:
    case 14:  // GHerpPlug_v1: policy 0x15, HerpAwake
    case 39:  // GOverlordPlug_v1: policy 0xb
        return Wake::Awake;
    case 18:  // GTurretPlug_v1: action 0, Idle
    case 37:  // GBoxerPlug_v1: its mind's slot 12
    case 40:  // GCimDronePlug_v1: action 0, Idle
        return Wake::Ready;
    default:  // GINPCPlug and the NPCs, GGooBeastPlug: an empty slot 1 (0x40e510)
        return Wake::None;
    }
}

Spawners::Spawners(Vfs& vfs, Level& level) : level_(level), sprites_(vfs) {
    for (const auto& g : level.map().gizmos) {
        const giz::Template& t = level.gizmoTemplate(g);
        if (!watched(t) || t.frames.empty()) continue;
        Watch w;
        w.gizmo = &g;
        const size_t last = t.frames.size() - 1;
        w.attachFrame = firstTagged(t, BeginActivate, 0);
        w.releaseFrame = firstTagged(t, EndActivate, last);
        // A spawner the level starts spent (state 3) has let its agents out.
        w.attached = w.released = level.gizmoState(g).state == 3;
        watches_.push_back(w);
    }
    for (const auto& a : level.map().agents) {
        const auto b = spawnBinding(a);
        if (!b || !level.agentVisible(a)) continue;
        const chk::GizmoRecord* g = level.gizmoByInstance(b->spawner);
        if (!g || level.gizmoTemplate(*g).cls != giz::Spawn) continue;
        slots_[b->spawner].push_back(&a);
        held_[a.instance] = level.gizmoState(*g).state != 3 && wakeFor(level.agentTemplate(a).cls) != Wake::None;
    }
}

bool Spawners::held(const chk::AgentRecord& a) const {
    const auto it = held_.find(a.instance);
    return it != held_.end() && it->second;
}

std::vector<const chk::AgentRecord*> Spawners::update(uint64_t nowMs) {
    std::vector<const chk::AgentRecord*> out;
    for (auto& w : watches_) {
        const uint16_t state = level_.gizmoState(*w.gizmo).state;
        if (state != 2 && state != 3) continue;
        // GSplitterPlug_v28 / GSpawnGizmoPlug_v28 test each frame step for
        // having crossed the frame; the rebuild tests the frame reached.
        const size_t frame = level_.gizmoFrame(*w.gizmo, nowMs);
        if (!w.attached && frame >= w.attachFrame) {
            w.attached = true;
            // GIAttachable slot 1 of each attached gizmo: the station halves
            // (splitters) and chained spawners all start the same way.
            for (const auto& [id, list] : level_.map().attachments) {
                if (id != w.gizmo->instance) continue;
                for (uint16_t other : list)
                    if (const chk::GizmoRecord* o = level_.gizmoByInstance(other))
                        level_.useGizmo(*o, nowMs, [](const chk::GizmoRecord&) { return false; });
            }
        }
        if (!w.released && frame >= w.releaseFrame && level_.gizmoTemplate(*w.gizmo).cls == giz::Spawn) {
            w.released = true;
            const auto it = slots_.find(w.gizmo->instance);
            if (it == slots_.end()) continue;
            for (const chk::AgentRecord* a : it->second) {
                if (!held(*a)) continue;
                held_[a->instance] = false;
                if (wakeFor(level_.agentTemplate(*a).cls) == Wake::Awake) awake_[a->instance] = {nowMs};
                out.push_back(a);
            }
        }
    }
    return out;
}

std::optional<Level::Pose> Spawners::pose(const chk::AgentRecord& a, uint64_t nowMs) {
    const bool isHeld = held(a);
    const auto awake = awake_.find(a.instance);
    if (!isHeld && awake == awake_.end()) return std::nullopt;

    Level::Pose pose;
    const auto anims = level_.agentTemplate(a).animsFor(kAwake);
    if (anims.empty()) {
        if (awake != awake_.end()) awake_.erase(awake);
        return isHeld ? std::optional<Level::Pose>(pose) : std::nullopt;  // held: nothing to show
    }
    const anm::Animation* anim = sprites_.anim(anims[a.instance % anims.size()]->path);
    if (!anim || anim->directions.empty()) return pose;
    const size_t dir = anm::directionFor(a.facing, anim->directions.size());
    if (dir >= anim->directions.size() || anim->directions[dir].frames.empty()) return pose;
    size_t frame = 0;
    if (!isHeld) {
        // The Awake action ends when its animation has played (GAction_v1).
        frame = size_t((nowMs - std::min(nowMs, awake->second.atMs)) / Level::kAgentFrameMs);
        if (frame >= anim->directions[dir].frames.size()) {
            awake_.erase(awake);
            return std::nullopt;
        }
    }
    pose.key = "spawn#" + std::to_string(a.instance) + "#" + std::to_string(dir) + "#" + std::to_string(frame);
    pose.image = sprites_.compose(pose.key, {anim}, dir, frame);
    if (!pose.image) return pose;
    pose.anchorX = anim->hotspot ? anim->hotspot->first : pose.image->width / 2;
    pose.anchorY = anim->hotspot ? anim->hotspot->second : pose.image->height;
    return pose;
}

}  // namespace harbinger::world
