// One interface (.itf) brought to life: widgets, hover/press handling, focus
// for keyboard and gamepad, drawing. Plus the animated mouse cursor.
#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "app/assets.hpp"
#include "formats/itf.hpp"
#include "ui/item_animation.hpp"

namespace harbinger::app {

// One control, reduced to what the front end needs.
struct Widget {
    const itf::Control* control = nullptr;
    SDL_FRect rect{};
    std::vector<const Texture*> images;  // Button: off, on, pressed
    Texture text;                         // StaticTextBox, owned
    // AnimatedBtn
    const Texture* strip = nullptr;
    int frameH = 0;
    ui::ItemAnimation anim;
    bool hovered = false, pressed = false, selected = false;
    uint32_t action = 0;

    bool interactive() const;
};

class Screen {
public:
    // Interface sound slots: 9 = hover, 10 = press (GCtrlAnimatedBtn_v9 / _v7).
    enum SoundSlot { kHoverSound = 9, kPressSound = 10 };
    using SoundFn = std::function<void(int slot, uint16_t soundId)>;

    Screen(Assets& assets, const std::string& itfPath, SoundFn sound = {});
    ~Screen();
    Screen(const Screen&) = delete;
    Screen& operator=(const Screen&) = delete;

    // Hover tracking; on `click`, presses the topmost widget under the point and
    // returns its action (else -1).
    long pointer(float x, float y, bool click);
    // Keyboard / gamepad: move the focus through the interactive widgets.
    void moveFocus(int delta);
    long activateFocus();
    // Holds the AnimatedBtn with this action in its pressed pose (character choice).
    void select(uint32_t action);

    void update(uint64_t nowMs);
    void draw(SDL_Renderer* ren) const;

private:
    void setHover(Widget& w, bool on);
    long activate(Widget& w);
    void playSlot(int slot);

    itf::Interface itf_;
    std::vector<gim::Color> palette_;
    std::vector<Widget> widgets_;
    std::vector<size_t> order_;
    std::optional<size_t> focus_;
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
