#include "ui/controls.hpp"

#include <algorithm>

namespace harbinger::ui {

bool Button::event(PointerEvent e) {
    switch (e) {
        case PointerEvent::Cancel: state = saved; break;
        case PointerEvent::Down:
            if (state != ButtonState::Disabled) state = ButtonState::Pressed;
            break;
        case PointerEvent::Up:
            if (state == ButtonState::Disabled) break;
            state = saved = ButtonState::Normal;
            return true;
        case PointerEvent::Held: break;
    }
    return false;
}

void CheckBox::set(bool on) {
    checked = on;
    state = saved = on ? ButtonState::Checked : ButtonState::Normal;
}

bool CheckBox::event(PointerEvent e) {
    if (e != PointerEvent::Up) {
        Button b{state, saved};
        b.event(e);
        state = b.state, saved = b.saved;
        if (checked) state = saved = ButtonState::Checked;  // no pressed look while checked
        return false;
    }
    checked = !checked;
    if (state == ButtonState::Pressed)
        state = saved = saved == ButtonState::Normal ? ButtonState::Checked : ButtonState::Normal;
    else if (state == ButtonState::Checked)
        state = saved = ButtonState::Normal;
    return true;
}

void SliderBar::set(double v) {
    reported = v;
    pos = int(v * range());  // __ftol truncates
}

bool SliderBar::event(PointerEvent e, int x, uint64_t nowMs) {
    switch (e) {
        case PointerEvent::Cancel:
        case PointerEvent::Up:
            leftHeld = rightHeld = false;
            return false;  // notifies with action -1, which the interfaces ignore
        case PointerEvent::Held:
            // GCtrlSliderBar_v7 repeats the press every 25 ms while held.
            if (nowMs - lastRepeatMs <= 25) return true;
            lastRepeatMs = nowMs;
            leftHeld = rightHeld = false;
            [[fallthrough]];
        case PointerEvent::Down:
            if (leftHeld || rightHeld) return true;
            if (x < arrow) {
                pos = std::max(pos - 3, 0);
                leftHeld = true;
            } else if (x > width - arrow) {
                pos = std::min(pos + 3, range());
                rightHeld = true;
            } else {
                pos = std::max(x - x % 3 - arrow, 0);
            }
            return true;
    }
    return false;
}

double SliderBar::changed() {
    const double v = value();
    if (v == reported) return -1.0;
    reported = v;
    return v;
}

void SliderBar::nudge(int dir) { pos = std::clamp(pos + dir * 18, 0, range()); }

}  // namespace harbinger::ui
