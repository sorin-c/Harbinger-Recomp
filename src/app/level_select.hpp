// The umbilical's destination list (the Level Selection interface,
// res\common\interface\level\level.itf): the names of the places the
// player's codes reach, gold ones first, picked with the mouse, the keys or
// the pad. Notes and evidence: docs/umbilicals.md.
#pragma once

#include <SDL3/SDL.h>

#include <optional>
#include <string>
#include <vector>

#include "app/assets.hpp"
#include "formats/itf.hpp"
#include "ui/controls.hpp"

namespace harbinger::app {

class LevelSelect {
public:
    LevelSelect(Assets& assets, int screenW, int screenH);
    ~LevelSelect();
    LevelSelect(const LevelSelect&) = delete;
    LevelSelect& operator=(const LevelSelect&) = delete;

    // Shows the list: one line per place, gold names starting with '*'
    // (UmbilicalCodes::listText).
    void open(const std::string& text);
    void close();
    bool isOpen() const { return open_; }

    // Mouse, in game screen pixels. A click on a name picks it, Cancel
    // cancels, the arrows scroll.
    void pointerMove(float x, float y);
    void pointerDown(float x, float y);
    void pointerUp(float x, float y);
    // Keys and pad (rebuild): move the highlight, pick it, cancel.
    void moveFocus(int delta);
    void pick();
    void cancel();

    // What the player did: the picked line (0 = the first name), -1 for
    // cancel; nothing yet while still choosing.
    std::optional<int> takeChoice();

    void draw(SDL_Renderer* ren) const;

private:
    struct Part {
        const itf::Control* control = nullptr;
        SDL_FRect rect{};
        std::vector<const Texture*> images;
        ui::Button button;
    };
    bool inside(const Part& p, float x, float y) const;
    int lineAt(float x, float y) const;
    int visibleLines() const;
    void scroll(int delta);
    void render();

    Assets& assets_;
    itf::Interface itf_;
    std::vector<gim::Color> palette_;
    std::vector<Part> parts_;  // drawn in this order
    Part* cancel_ = nullptr;
    Part* up_ = nullptr;
    Part* down_ = nullptr;
    const Part* box_ = nullptr;
    std::vector<std::string> lines_;
    int top_ = 0;        // first line shown
    int highlight_ = -1; // line under the pointer or the pad focus (absolute)
    Part* pressed_ = nullptr;
    bool open_ = false;
    std::optional<int> choice_;
    Texture face_{};
};

}  // namespace harbinger::app
