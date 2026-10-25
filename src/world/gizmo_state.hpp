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
        Once,      // first..last (last..first when `reverse`) once, then holds the end
    };
    Mode mode = Hold;
    size_t first = 0, last = 0;
    bool reverse = false;  // Once
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

// A placed gizmo's state (plug+0x7c) and what it depends on.
struct GizmoState {
    uint16_t state = 0;
    int power = 0, needed = 0;  // GIPowerable: what it has and needs
    uint32_t phaseMs = 0;       // Looped: start offset
};

// The state a gizmo record starts in, from its class data. `power` is the
// instance's startingPower.
GizmoState startingState(const giz::Template& t, const chk::GizmoRecord& g, int power);
// How a gizmo plays in a state (each class's plug slot 17 and 29).
GizmoPlay playFor(const giz::Template& t, const GizmoState& st);
// playFor(startingState(...)).
GizmoPlay startingPlay(const giz::Template& t, const chk::GizmoRecord& g, int power);

// Using gizmos. A player uses an activatable gizmo (consoles, chests);
// a console then toggles the gizmos attached to it (doors, bridges). Each
// returns true if the state changed.
bool usable(const giz::Template& t);
// GIActivatable slot 1 (GConsolePlug_v1, GBarrelPlug_v1, GSpawnGizmoPlug_v1;
// triggers and scripts activate spawners, players do not).
bool activate(const giz::Template& t, GizmoState& st);
// GIAttachable slot 1 (GBridgePlug_v1). `occupied`: an agent stands on the
// gizmo, so it may not shut.
bool toggle(const giz::Template& t, GizmoState& st, bool occupied);
// The state a moving gizmo settles in when its frames have played
// (GBridgePlug_v29, GBarrelPlug_v29), or a console once what it toggled has
// settled (GConsolePlug_v0).
bool finish(const giz::Template& t, GizmoState& st);
// How long a Once play lasts; 0 for plays that do not end.
uint64_t playLength(const giz::Template& t, const GizmoPlay& play);

// The frame shown `ms` into `play`. `seed` stands in for the original's
// random numbers (Pauses), so a gizmo plays the same way every time.
size_t playFrame(const giz::Template& t, const GizmoPlay& play, uint64_t ms, uint32_t seed);

}  // namespace harbinger::world
