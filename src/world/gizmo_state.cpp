#include "world/gizmo_state.hpp"

#include <algorithm>
#include <set>

namespace harbinger::world {

namespace {

constexpr size_t npos = size_t(-1);

size_t firstTag(const giz::Template& t, uint16_t tag, size_t from = 0) {
    for (size_t i = from; i < t.frames.size(); ++i)
        if (t.frames[i].code == tag) return i;
    return npos;
}

uint16_t u16At(const std::vector<uint8_t>& d, size_t at) {
    return at + 2 <= d.size() ? uint16_t(d[at] | d[at + 1] << 8) : 0;
}

uint32_t u32At(const std::vector<uint8_t>& d, size_t at) {
    return at + 4 <= d.size() ? uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 |
                                    uint32_t(d[at + 3]) << 24
                              : 0;
}

GizmoPlay hold(size_t frame) {
    GizmoPlay p;
    p.first = p.last = frame;
    return p;
}

GizmoPlay range(GizmoPlay::Mode mode, size_t first, size_t last) {
    if (first == npos || last == npos || first > last) return hold(first == npos || first > last ? 0 : first);
    GizmoPlay p;
    p.mode = first == last ? GizmoPlay::Hold : mode;
    p.first = first;
    p.last = last;
    return p;
}

// Breakers (FUN_00439650): `on` is the frame after the EndOpen frame, `close`
// the BeginClose frame (the frame count when there is none).
void breakerFrames(const giz::Template& t, size_t& on, size_t& close) {
    on = 0;
    close = t.frames.size();
    const size_t endOpen = firstTag(t, EndOpen);
    if (endOpen == npos || endOpen + 1 >= t.frames.size()) return;
    on = endOpen + 1;
    if (const size_t c = firstTag(t, BeginClose, on); c != npos) close = c;
}

bool isBreaker(const giz::Template& t) { return t.cls == giz::Breaker; }

// Per-cycle numbers for Pauses, from the seed and the cycle number.
uint32_t mix(uint32_t seed, uint32_t n) {
    uint32_t x = seed * 0x9e3779b9u ^ n * 0x85ebca6bu;
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

uint64_t duration(const giz::Template& t, size_t first, size_t last) {
    uint64_t total = 0;
    for (size_t i = first; i <= last && i < t.frames.size(); ++i) total += t.frames[i].durationMs;
    return total;
}

}  // namespace

std::map<uint16_t, int> startingPower(const chk::Level& level, const TemplateOf& templateOf) {
    std::map<uint16_t, const chk::GizmoRecord*> byInstance;
    for (const auto& g : level.gizmos) byInstance[g.instance] = &g;
    std::map<uint16_t, const std::vector<uint16_t>*> targets;
    for (const auto& [id, list] : level.powers) targets[id] = &list;

    std::map<uint16_t, int> power;
    // GIPowerable_v0: add the power, then hand the same amount to each target;
    // a breaker hands it on only in state 3 (GBreakerPlug_v0). The path guard
    // stops a loop of links from running forever.
    std::set<uint16_t> path;
    std::function<void(uint16_t, int)> give = [&](uint16_t id, int amount) {
        power[id] += amount;
        const auto g = byInstance.find(id);
        if (g != byInstance.end()) {
            const giz::Template& t = templateOf(*g->second);
            if (isBreaker(t) && u16At(g->second->extra, 0) != 3) return;
        }
        const auto it = targets.find(id);
        if (it == targets.end() || !path.insert(id).second) return;
        for (uint16_t to : *it->second) give(to, amount);
        path.erase(id);
    };
    for (const auto& [id, amount] : level.powerSources) give(id, amount);
    return power;
}

GizmoPlay startingPlay(const giz::Template& t, const chk::GizmoRecord& g, int power) {
    const size_t n = t.frames.size();
    if (n == 0) return hold(0);
    const size_t last = n - 1;
    const auto& d = g.extra;
    switch (t.cls) {
    case giz::Looped:
    case giz::Looped2: {
        // GLoopedGizmoPlug_v22: u32 phase (plug+0x34), u16 mode (plug+0x7c).
        // GLoopedGizmoPlug_v17: mode 3 loops from the BeginIdle frame to the
        // end, the other modes over the frames before it (all of them when
        // there is none). Looped2 also runs backwards at times
        // (GLoopedGizmoPlug_v29); the rebuild plays it forwards.
        const size_t start = t.loopStart();
        size_t first = 0, end = last;
        if (u16At(d, 4) == 3) first = start;
        else if (start > 0) end = start - 1;
        GizmoPlay p = range(GizmoPlay::Loop, first, end);
        p.phaseMs = u32At(d, 0);
        return p;
    }
    case giz::Bridge:
    case giz::TwoWay:
        // GBridgePlug_v22: u16 state; 0 shows the first frame, anything else
        // the last (3 when saved, GBridgePlug_v21). GBridgePlug_v17/v28: it
        // only moves while opening or closing.
        return hold(u16At(d, 0) == 0 ? 0 : last);
    case giz::Chest:
    case giz::Barrel:
    case giz::TrashPile: {
        // GBarrelPlug_v22: label, six bytes, then u16 state. GBarrelPlug_v17:
        // states 0 and 4 loop the frames before BeginOpen (GBarrelPlug_v29
        // goes back to frame 0), 2 plays the opening, the rest show the last.
        const uint16_t state = d.size() >= 2 ? u16At(d, d.size() - 2) : 0;
        const size_t open = firstTag(t, BeginOpen);  // FUN_00444710
        if (state == 0 || state == 4) return open == npos || open == 0 ? hold(0) : range(GizmoPlay::Loop, 0, open - 1);
        return hold(last);
    }
    case giz::Console:
    case giz::CompuConsole: {
        // GConsolePlug_v22: u16 state, u16, u16 power needed (GIPowerable,
        // plug+0xde), label, ... GConsolePlug_v17 / GCompuConsolePlug_v17:
        // without enough power the console shows its last frame; otherwise
        // it idles over the frames before BeginActivate (0x453800), forth
        // and back for a Console (GConsolePlug_v29), looping for a
        // CompuConsole (GCompuConsolePlug_v29).
        const uint16_t state = u16At(d, 0);
        const int needed = int16_t(u16At(d, 4));
        const size_t act = firstTag(t, BeginActivate) == npos ? 0 : firstTag(t, BeginActivate);
        if (state != 2 && power < needed) return hold(last);
        if (state == 4 || state == 2) return hold(act);
        if (t.cls == giz::CompuConsole && state == 5) return hold(n >= 2 ? n - 2 : 0);
        return act == 0 ? hold(0) : range(t.cls == giz::Console ? GizmoPlay::PingPong : GizmoPlay::Loop, 0, act - 1);
    }
    case giz::Breaker: {
        // GBreakerPlug_v22: u16 state, u16 power needed. GBreakerPlug_v17:
        // 0 (off) shows the first frame; 3 and 5 (on) loop from after EndOpen
        // to before BeginClose.
        const uint16_t state = u16At(d, 0);
        size_t on, close;
        breakerFrames(t, on, close);
        if (state == 3 || state == 5) return close == 0 ? hold(0) : range(GizmoPlay::Loop, on, close - 1);
        return hold(0);
    }
    case giz::Incubator:
    case giz::RandomPause: {
        // GIncubatorPlug_v22: u16 power needed (plug+0xc6). FUN_004f9670:
        // each BeginIdle frame starts a sequence, and the gizmo rests on the
        // frame before the first. GIncubatorPlug_v28/v29: with enough power it
        // waits (template u32 min, u32 max, in 10 ms), plays a random
        // sequence and rests again; without it, it shows frame 0.
        const int needed = int16_t(u16At(d, 0));
        const size_t first = firstTag(t, BeginIdle);
        if (first == npos || first == 0 || power < needed) return hold(0);
        GizmoPlay p;
        p.mode = GizmoPlay::Pauses;
        p.first = p.last = first - 1;
        for (size_t i = first; i < n; ++i)
            if (t.frames[i].code == BeginIdle) {
                if (!p.sequences.empty()) p.sequences.back().second = i - 1;
                p.sequences.emplace_back(i, last);
            }
        p.minPauseMs = u32At(t.classData, 0);
        p.maxPauseMs = u32At(t.classData, 4);
        return p;
    }
    default:
        return hold(0);
    }
}

size_t playFrame(const giz::Template& t, const GizmoPlay& play, uint64_t ms, uint32_t seed) {
    if (t.frames.empty()) return 0;
    switch (play.mode) {
    case GizmoPlay::Hold:
        return std::min(play.first, t.frames.size() - 1);
    case GizmoPlay::Loop:
        return t.frameAt(ms + play.phaseMs, play.first, play.last);
    case GizmoPlay::PingPong: {
        const uint64_t one = duration(t, play.first, play.last);
        if (one == 0) return play.first;
        ms %= 2 * one;
        if (ms < one) return t.frameAt(ms, play.first, play.last);
        // Backwards: the same walk from the other end.
        uint64_t left = ms - one, sum = 0;
        for (size_t i = play.last + 1; i-- > play.first;) {
            sum += t.frames[i].durationMs;
            if (left <= sum) return i;
        }
        return play.first;
    }
    case GizmoPlay::Pauses: {
        if (play.sequences.empty()) return play.first;
        // GIncubatorPlug_v29: a pause of (rand % (max - min) + min) * 10 ms,
        // or max ms when the two are equal, then a random sequence. The
        // rebuild draws its numbers from the seed and repeats a run of 64
        // pauses, so a long session never walks far.
        constexpr uint32_t kRun = 64;
        auto pauseOf = [&](uint32_t i) -> uint64_t {
            const uint32_t span = play.maxPauseMs - play.minPauseMs;
            return span == 0 || play.maxPauseMs < play.minPauseMs
                       ? play.maxPauseMs
                       : uint64_t(mix(seed, 2 * i) % span + play.minPauseMs) * 10;
        };
        auto sequenceOf = [&](uint32_t i) { return play.sequences[mix(seed, 2 * i + 1) % play.sequences.size()]; };
        uint64_t period = 0;
        for (uint32_t i = 0; i < kRun; ++i) {
            const auto [a, b] = sequenceOf(i);
            period += pauseOf(i) + duration(t, a, b);
        }
        if (period == 0) return play.first;
        ms %= period;
        for (uint32_t i = 0; i < kRun; ++i) {
            const uint64_t pause = pauseOf(i);
            if (ms < pause) return play.first;
            ms -= pause;
            const auto [a, b] = sequenceOf(i);
            const uint64_t seq = duration(t, a, b);
            if (ms < seq) return t.frameAt(ms, a, b);
            ms -= seq;
        }
        return play.first;
    }
    }
    return 0;
}

}  // namespace harbinger::world
