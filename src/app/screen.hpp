// One interface (.itf) brought to life: widgets, pointer handling, focus for
// keyboard and gamepad, drawing. Plus the animated mouse cursor.
#pragma once

#include <SDL3/SDL.h>

#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "app/assets.hpp"
#include "formats/itf.hpp"
#include "ui/controls.hpp"
#include "ui/item_animation.hpp"
#include "ui/text.hpp"

namespace harbinger::app {

// One control, reduced to what the front end needs.
struct Widget {
    const itf::Control* control = nullptr;
    SDL_FRect rect{};
    std::vector<const Texture*> images;
    std::string text;  // StaticTextBox text or TextBtn caption
    Texture face[2];   // owned: StaticTextBox text; TextBtn normal and pressed faces
    // AnimatedBtn, Icon
    const Texture* strip = nullptr;
    int frameH = 0;
    ui::ItemAnimation anim;
    bool hovered = false, selected = false;
    // Interactive controls
    uint32_t action = 0;
    ui::Button button;
    ui::CheckBox check;
    ui::SliderBar slider;
    int step = 0;  // Choice: -1 or +1 from the last press

    uint16_t cls() const { return control->classId; }
    bool interactive() const;
};

// What a control tells its interface: the control's index in the .itf and
// its action id (message 0x18).
struct Notice {
    size_t control;
    long action;
};

class Screen {
public:
    // Interface sound slots: 9 = hover, 10 = press (GCtrlAnimatedBtn_v9 / _v7).
    enum SoundSlot { kHoverSound = 9, kPressSound = 10 };
    // `restart` false leaves a slot that is still sounding alone (FUN_004c6720).
    using SoundFn = std::function<void(int slot, uint16_t soundId, bool restart)>;

    Screen(Assets& assets, const std::string& itfPath, SoundFn sound = {});
    ~Screen();
    Screen(const Screen&) = delete;
    Screen& operator=(const Screen&) = delete;

    uint16_t id() const { return itf_.kind; }  // the interface id the handlers switch on

    // Mouse, in 800x600 screen coordinates. A press is captured by the control
    // under the pointer and ends on it: released over it, that is a click.
    void pointerMove(float x, float y);
    void pointerDown(float x, float y);
    void pointerUp(float x, float y);

    // Keyboard / gamepad: move the focus through the interactive controls,
    // press the focused one, or step the focused slider.
    void moveFocus(int delta);
    void activateFocus();
    void adjustFocus(int dir);

    // Notices since the last call, oldest first.
    std::vector<Notice> takeNotices();
    void playSlot(int slot, bool restart = true);

    // What an interface handler does to its controls, by .itf index.
    void select(uint32_t action);  // holds the AnimatedBtn with this action pressed
    void setSlider(size_t control, uint32_t action, double value);
    double sliderChanged(size_t control);  // message 0x28: -1 if unchanged
    void setCheck(size_t control, uint32_t action, bool on);
    bool checked(size_t control) const;
    void setText(size_t control, const std::string& text);
    void setDisabled(size_t control, bool disabled);

    // A rebuild-only control: one line of text in `font`, centred in `width`
    // pixels, that steps through choices. Clicking its left half or pressing
    // left asks for the previous choice, the right half or right the next;
    // the step is read with choiceStep(). Returns its control index.
    size_t addChoice(int x, int y, int width, uint16_t font, uint32_t action);
    int choiceStep(size_t control) const { return widgets_.at(control).step; }

    void update(uint64_t nowMs);
    void draw(SDL_Renderer* ren) const;

private:
    Widget* at(float x, float y);
    void event(Widget& w, ui::PointerEvent e);
    void setHover(Widget& w, bool on);
    void renderFaces(Widget& w);

    Assets& assets_;
    itf::Interface itf_;
    std::vector<gim::Color> palette_;
    std::vector<Widget> widgets_;
    std::deque<itf::Control> extras_;  // controls added by addChoice()
    std::vector<size_t> order_;
    std::optional<size_t> focus_, captured_;
    float pointerX_ = 0;
    uint64_t now_ = 0;
    std::vector<Notice> notices_;
    SoundFn sound_;
};

// The animated mouse cursor (cursor.mgr id 1, Main.acr).
class Cursor {
public:
    explicit Cursor(Assets& assets);
    void draw(SDL_Renderer* ren, float x, float y, uint64_t nowMs) const;

private:
    std::vector<const Texture*> frames_;
    float hotX_ = 0, hotY_ = 0;
    uint64_t delayMs_ = 100;
};

}  // namespace harbinger::app
