// The conversation screen (res\common\interface\dialog\npcdialog.itf,
// interface kind 6): the NPC's portrait, what it says (scrolled with the up
// and down arrows) and up to four responses the player picks from. Drawn
// from the .itf over the game view; the rebuild adds the keyboard and
// gamepad (the responses as a focus list, A or Enter picks, B or Escape
// closes). Notes and evidence: docs/dialogue.md
#pragma once

#include <SDL3/SDL.h>

#include <optional>
#include <string>
#include <vector>

#include "app/assets.hpp"
#include "app/screen.hpp"
#include "formats/itf.hpp"
#include "ui/controls.hpp"
#include "world/story.hpp"

namespace harbinger::app {

class Dialogue {
public:
    // `screenW` x `screenH` is the game view's size; the screen is laid
    // out for 800x600 and centred in it.
    Dialogue(Assets& assets, int screenW, int screenH, Screen::SoundFn sound = {});
    ~Dialogue();
    Dialogue(const Dialogue&) = delete;
    Dialogue& operator=(const Dialogue&) = delete;

    // Shows what the story's conversation is at now (rebuilt when its node
    // changes).
    void sync(const world::Story& story);

    // Mouse, in game screen pixels: the responses and the scroll arrows
    // (pressed and released over them).
    void pointerMove(float x, float y);
    void pointerDown(float x, float y);
    void pointerUp(float x, float y);
    void wheel(int lines) { scroll(lines); }
    // Keyboard and pad: move the focus up or down the responses, pick the
    // focused one, scroll the text.
    void moveFocus(int delta);
    void activateFocus();
    void scroll(int lines);
    bool usingFocus() const { return focus_.has_value(); }

    // The response the player picked (an index into the story's
    // responses()), once.
    std::optional<size_t> takeChoice();

    void update(uint64_t nowMs);
    void draw(SDL_Renderer* ren) const;

private:
    struct Button {
        SDL_FRect rect{};
        ui::Button button;
        bool hovered = false;
    };
    void layout(const world::Story& story);
    void renderText();
    void updateArrows();
    int at(float x, float y) const;  // response index, or kUp / kDown, or -1
    void press(int which, ui::PointerEvent e);
    void sound(int slot) const;

    static constexpr int kUp = 100, kDown = 101;

    Assets& assets_;
    Screen::SoundFn sound_;
    itf::Interface itf_;
    std::vector<gim::Color> palette_;
    float x_ = 0, y_ = 0;  // the background's screen position
    const itf::Control *background_ = nullptr, *text_ = nullptr, *portrait_ = nullptr, *upCtl_ = nullptr,
                       *downCtl_ = nullptr;
    std::vector<const itf::Control*> responseCtl_;
    const Texture* backgroundTex_ = nullptr;
    const Texture *up_[3] = {}, *down_[3] = {};
    Button upBtn_, downBtn_;

    uint64_t revision_ = ~0ull;
    std::string words_;
    std::vector<std::string> lines_;  // the words, wrapped
    int top_ = 0;                     // the first line shown
    int visible_ = 0;                 // how many lines the box shows
    itf::TextBox box_;
    Texture textTex_{};
    SDL_FRect textRect_{};

    struct Response {
        Button b;
        Texture face[2];  // normal, pressed
    };
    std::vector<Response> responses_;
    std::optional<size_t> choice_;
    std::optional<size_t> focus_;
    int captured_ = -1;

    uint16_t portraitId_ = 0;
    const Texture* portraitTex_ = nullptr;
    SDL_FRect portraitSrc_{}, portraitDst_{};
};

}  // namespace harbinger::app
