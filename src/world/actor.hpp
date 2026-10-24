// What the player and the enemies share as bodies on the map: walking a
// path, swinging a melee animation and where its blows land, and the boxes
// blows are tested against. Notes and evidence: docs/movement.md,
// docs/combat.md, docs/enemies.md
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "formats/anm.hpp"
#include "world/walkmap.hpp"

namespace harbinger::world {

// FUN_0048b510: does the segment from (ax, ay) to (bx, by) touch the square
// of half-size h around (cx, cy)? The original clips the segment against the
// square by outcodes; this is the same test by slab clipping.
bool segmentTouchesBox(double ax, double ay, double bx, double by, double cx, double cy, double h);

// FUN_0042a6a0: the map offset, from the agent, of the blow tag at `frame`
// in direction `dir` of a melee animation (its layers, body first), if that
// frame has one. The tags of one frame, across all directions, circle the
// agent; their mean |y| is the circle's centre row. A tag's offset from it,
// (dx, dy) = ((x - hotspot x) / 128, (|y| - mean) / 64), is a screen offset
// turned into the map: (dy + dx, dy - dx).
std::optional<WalkMap::Point> blowOffset(const std::vector<const anm::Animation*>& anims, size_t dir, uint16_t frame);

// One step along a click or AI path (GAgentPlug_v80, FUN_0041e200): turns
// to the next waypoint, moves `step` cells, and drops the waypoint once an
// axis has gone past it (moves under 0.00001 count as none).
void followPath(std::vector<WalkMap::Point>& path, double& x, double& y, uint8_t& facing, double step);

// A melee swing (agent state 4) driven by its animation's action tags
// (GAgentPlug_v9, 0x41f330). The swing's clock starts at start(); frames
// last `frameMs`.
class Swing {
public:
    void start(uint64_t nowMs) { start_ = nowMs, frame_ = -1; }
    uint64_t startMs() const { return start_; }

    struct Result {
        bool ended = false;                   // the animation ran out: the swing is over
        std::optional<WalkMap::Point> blow;   // a blow tag was reached: its map offset
    };
    // Runs the tags of every frame reached since the last call, in the
    // direction `facing` gives (read again after each repeat). At a -3 tag,
    // `repeat` decides whether the swing goes round again (it may turn the
    // agent to its target); if so the clock moves back by the tag's y frames.
    Result advance(const std::vector<const anm::Animation*>& anims, const uint8_t& facing, uint64_t nowMs,
                   uint64_t frameMs, const std::function<bool()>& repeat);

private:
    uint64_t start_ = 0;
    int64_t frame_ = -1;  // the last frame whose tags have run
};

}  // namespace harbinger::world
