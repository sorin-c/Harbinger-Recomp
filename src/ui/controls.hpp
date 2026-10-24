// Behaviour of the interactive interface controls, free of any rendering:
// the engine's control state, and how mouse events change it.
#pragma once

#include <cstdint>

namespace harbinger::ui {

// Mouse events as the engine passes them to a control's vtable slot 7.
enum class PointerEvent {
    Cancel = 0,  // released outside the control, or the press was taken away
    Down = 1,
    Held = 2,  // sent every frame while the button stays down
    Up = 3,    // released over the control: a click
};

// GCtrlButton state (+0x64): which image a Button, TextBtn or ScrollButton
// shows. Images are numbered from 0 in .itf order: Button / ScrollButton draw
// image 1 normally, 2 while pressed and 0 when disabled (GCtrlButton_v14);
// TextBtn draws image 0, and image 1 with its second font while pressed
// (GCtrlTextBtn_v3).
enum class ButtonState { Normal = 0, Pressed = 1, Checked = 2, Disabled = 3 };

// GCtrlButton_v7: press shows the pressed look, the click (Up) sends the
// action. Returns true when the action is sent.
struct Button {
    ButtonState state = ButtonState::Normal, saved = ButtonState::Normal;
    bool event(PointerEvent e);
};

// GCtrlCheckBox_v7 / _v14. Image 0 is "on", image 1 "off"; while pressed it
// shows image 0 whatever its value. Clicking flips the value and sends the action.
struct CheckBox {
    bool checked = false;
    ButtonState state = ButtonState::Normal, saved = ButtonState::Normal;
    void set(bool on);  // message 0x27
    bool event(PointerEvent e);
    int image() const { return state == ButtonState::Normal ? 1 : 0; }
};

// GCtrlSliderBar: a track `width` pixels wide with an arrow `arrow` pixels
// wide at each end. The value is a pixel position along the track between
// the arrows, kept on a 3-pixel grid when set by pointing.
struct SliderBar {
    int width = 400, arrow = 25;
    int pos = 0;
    bool leftHeld = false, rightHeld = false;
    uint64_t lastRepeatMs = 0;
    double reported = 0;  // value last returned by changed()

    int range() const { return width - 2 * arrow; }
    double value() const { return double(pos) / double(range()); }
    void set(double v);  // message 0x27
    // `x` is the pointer relative to the control. Returns true when the
    // control notifies its interface, which then reads changed().
    bool event(PointerEvent e, int x, uint64_t nowMs);
    // Message 0x28: the value if it differs from the last one read, else -1.
    double changed();
    // Keyboard / gamepad step (a rebuild addition): `dir` arrow presses.
    void nudge(int dir);
};

}  // namespace harbinger::ui
