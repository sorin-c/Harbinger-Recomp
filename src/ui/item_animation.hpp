// Frame stepping of animated items (GAnimatedItemPlug), used by the front-end
// buttons: hovering loops a section of the strip, leaving plays it back to rest,
// clicking plays the press section. See docs/formats/itm.md.
#pragma once

#include <cstdint>

#include "formats/itm.hpp"

namespace harbinger::ui {

class ItemAnimation {
public:
    enum class State { Idle = 0, Hover = 1, Reset = 5, Pressed = 6 };

    ItemAnimation() = default;
    ItemAnimation(const itm::Animation& marks, uint16_t frameCount) : marks_(marks), count_(frameCount) {}

    void setState(State s) { state_ = s; }
    State state() const { return state_; }
    uint16_t frame() const { return frame_; }

    // Advances at most one frame, and only once more than 100 ms have passed
    // since the last step (the engine's timing).
    void update(uint64_t nowMs);
    // One step of the engine's state machine, without the timing.
    void step();

private:
    itm::Animation marks_;
    uint16_t count_ = 1;
    State state_ = State::Idle;
    uint16_t frame_ = 0;
    uint64_t last_ = 0;
    bool started_ = false;
};

}  // namespace harbinger::ui
