#include "ui/item_animation.hpp"

namespace harbinger::ui {

void ItemAnimation::update(uint64_t nowMs) {
    if (!started_) {
        started_ = true;
        last_ = nowMs;
    }
    const uint64_t elapsed = nowMs - last_;
    if (elapsed <= 100) return;
    last_ += 100 * ((elapsed - 1) / 100);
    step();
}

// Transcribed from GAnimatedItemPlug_v3 (0x4df300). Frame numbers are u16 in
// the engine, so the "start - 1" targets wrap when a mark is 0.
void ItemAnimation::step() {
    const uint16_t f = frame_, a = marks_.loopStart, b = marks_.loopEnd, c = marks_.pressEnd;
    switch (state_) {
        case State::Idle:
            // Finish the hover loop, then run backwards to frame 0 (or forwards
            // through any frames past the press section, wrapping to pressEnd - 1).
            if (a < f && f < b) {
                frame_ = uint16_t(f + 1);
                if (frame_ >= b) frame_ = uint16_t(a - 1);
            } else if (f == b) {
                frame_ = uint16_t(a - 1);
            } else if (b < f && f <= c) {
                frame_ = uint16_t(f - 1);
            } else if (f <= c) {
                if (f != 0) frame_ = uint16_t(f - 1);
            } else {
                frame_ = uint16_t(f + 1);
                if (frame_ >= count_) frame_ = uint16_t(c - 1);
            }
            break;
        case State::Hover:
            frame_ = uint16_t(f + 1);
            if (frame_ >= b) frame_ = a;
            break;
        case State::Reset:
            frame_ = 0;
            started_ = false;
            state_ = State::Idle;
            break;
        case State::Pressed:
            frame_ = uint16_t(f + 1);
            if (a <= frame_ && frame_ < b) frame_ = b;
            if (frame_ >= count_) frame_ = c;
            break;
    }
}

}  // namespace harbinger::ui
