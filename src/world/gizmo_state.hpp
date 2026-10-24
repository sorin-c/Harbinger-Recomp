// Gizmo states: which frames a placed gizmo plays in the state the level
// starts it in (doors open or shut, chests closed, consoles idling or dead,
// breakers on or off), and the power that breakers pass on. Notes and
// evidence: docs/formats/gizmo.md, "States".
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "formats/chk.hpp"
#include "formats/giz.hpp"

namespace harbinger::world {

// Frame tags: a frame's code names the state sequence it begins or ends.
// The names table at 0x57df38 (32 bytes each) is in code order.
enum Tag : uint16_t {
    NoTag = 0, BeginIdle = 1, EndIdle = 2, BeginOpen = 3, EndOpen = 4, BeginFire = 5, EndFire = 6,
    BeginActivate = 7, EndActivate = 8, BeginClose = 9, EndClose = 10,
};

// How a gizmo animates in one state.
struct GizmoPlay {
    enum Mode : uint8_t {
        Hold,      // shows `first`
        Loop,      // first..last, then again
        PingPong,  // first..last, last..first (each end shown twice, GConsolePlug_v29)
        Pauses,    // rests on `first`, then plays one of `sequences` after a pause
    };
    Mode mode = Hold;
    size_t first = 0, last = 0;
    uint32_t phaseMs = 0;  // Loop: where in the loop the level starts it
    // Pauses (GIncubatorPlug): each sequence is a frame range; a pause lasts
    // minPauseMs..maxPauseMs.
    std::vector<std::pair<size_t, size_t>> sequences;
    uint32_t minPauseMs = 0, maxPauseMs = 0;
};

using TemplateOf = std::function<const giz::Template&(const chk::GizmoRecord&)>;

// The power each gizmo instance has when the level starts. The level names
// the gizmos that start with power (chk::Level::powerSources) and, per gizmo,
// the gizmos it passes power to (chk::Level::powers). A gizmo passes on all
// the power it gets (GIPowerable_v0, 0x4d4ab0); a breaker only while it is on
// (GBreakerPlug_v0, state 3).
std::map<uint16_t, int> startingPower(const chk::Level& level, const TemplateOf& templateOf);

// The state a gizmo record starts in, from its class data, and how it plays.
// `power` is the instance's startingPower.
GizmoPlay startingPlay(const giz::Template& t, const chk::GizmoRecord& g, int power);

// The frame shown `ms` into `play`. `seed` stands in for the original's
// random numbers (Pauses), so a gizmo plays the same way every time.
size_t playFrame(const giz::Template& t, const GizmoPlay& play, uint64_t ms, uint32_t seed);

}  // namespace harbinger::world
