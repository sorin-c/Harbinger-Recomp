// The in-game HUD: the character's left panel (health bar, warning light,
// health packs), right panel (energy, the CHAR and MAP buttons), the log
// button, and the inventory screen the CHAR button opens (equipment slots
// with their mod chip sockets, the item grid, money, item descriptions).
// Drawn from the character's .itf files, placed the way FUN_004c4af0 places
// each panel kind. The rebuild adds gamepad navigation of the inventory in
// the style of Diablo II: Resurrected / Diablo III.
// Notes and evidence: docs/inventory.md
#pragma once

#include <SDL3/SDL.h>

#include <optional>
#include <string>
#include <vector>

#include "app/assets.hpp"
#include "app/screen.hpp"
#include "formats/itf.hpp"
#include "world/inventory.hpp"
#include "world/item_catalog.hpp"
#include "world/player.hpp"

namespace harbinger::app {

class Hud {
public:
    // What the HUD shows of the player, as fractions of the maximum.
    struct Vitals {
        double hitPoints = 1, maxHitPoints = 1;
        double energy = 1, maxEnergy = 1;
    };
    enum class Action { UseHealthPack, Map, QuestLog };

    Hud(Assets& assets, world::Character character, int screenW, int screenH, Screen::SoundFn sound = {});
    ~Hud();
    Hud(const Hud&) = delete;
    Hud& operator=(const Hud&) = delete;

    // The grid size and equipment slots of a character's inventory screen,
    // read from its inventory.itf (the slots in file order; a socket belongs
    // to the slot before it).
    static world::Inventory::Layout inventoryLayout(Assets& assets, world::Character character);

    void attach(world::Inventory* inventory, world::ItemCatalog* catalog);

    bool inventoryOpen() const { return open_; }
    // Closing puts a held item back in the grid first; false if it has no room.
    bool setInventoryOpen(bool open);
    bool toggleInventory() { return setInventoryOpen(!open_); }
    // The screen x the world view centres on: the middle of the part the
    // inventory leaves uncovered while it is open.
    double viewCentreX() const;

    // Mouse, in game screen pixels. pointerDown returns true when the HUD
    // takes the press (it is over a panel), so the world does not see it.
    void pointerMove(float x, float y);
    bool pointerDown(float x, float y);
    // The secondary button over an item: quick move (equip it, or take it
    // off into the grid). True if taken.
    bool pointerAlt(float x, float y);
    // Whether (x, y) is over a drawn part of a panel.
    bool covers(float x, float y) const;

    // Gamepad inside the open inventory: move the focus one step (d-pad, or
    // the stick with repeat), A picks up / puts down, X moves the focused
    // item to its slot or back to the grid. Using the pad hides the mouse
    // pointer's item and tooltip in favour of the focus.
    void padMove(int dx, int dy);
    void padStick(float x, float y);
    void padPress();
    void padQuick();
    bool usingPad() const { return pad_; }

    std::vector<Action> takeActions();
    void update(uint64_t nowMs, const Vitals& vitals);
    void draw(SDL_Renderer* ren) const;

private:
    struct Control {
        const itf::Control* control = nullptr;
        SDL_FRect rect{};
        std::vector<const Texture*> images;
        int slot = -1;  // inventory slot index of an equipment slot
        bool hovered = false;
    };
    struct Panel {
        itf::Interface itf;
        std::vector<gim::Color> palette;
        float x = 0, y = 0;  // the background's screen position
        render::RgbaImage mask;  // the background, for hit tests
        std::vector<Control> controls;  // drawn in this order
        bool hit(float px, float py) const;
    };
    // A place the pad focus can rest on: an equipment slot or a grid cell.
    struct Focus {
        int slot = -1;
        int cx = 0, cy = 0;
    };

    void load(Panel& p, const std::string& path);
    const Control* find(const Panel& p, uint16_t cls) const;
    SDL_FRect cellRect(int cx, int cy) const;
    std::optional<std::pair<int, int>> cellAt(float x, float y) const;
    int slotAt(float x, float y) const;
    SDL_FRect focusRect(const Focus& f) const;
    bool maskAt(float x, float y) const;
    void clickInventory(float x, float y);
    void clickSlot(int slot);
    void clickCell(int cx, int cy);
    void quickMove(std::optional<Focus> at);
    void sound(int slot) const;
    void clampFocus();
    void setTooltip(const std::string& text, float anchorX, float anchorY, float bottomY);

    void drawPanel(SDL_Renderer* ren, const Panel& p) const;
    void drawControl(SDL_Renderer* ren, const Panel& p, const Control& c) const;
    void drawItem(SDL_Renderer* ren, uint16_t id, float x, float y, bool centred) const;
    void drawInventory(SDL_Renderer* ren) const;
    const Texture* itemTexture(uint16_t id, SDL_FRect& src) const;

    Assets& assets_;
    world::Character character_;
    int screenW_, screenH_;
    Screen::SoundFn sound_;
    Panel left_, right_, log_, inv_, popup_;
    world::Inventory* inventory_ = nullptr;
    world::ItemCatalog* catalog_ = nullptr;
    bool open_ = false;
    std::vector<Action> actions_;
    uint64_t now_ = 0, lastUpdate_ = 0;
    Vitals vitals_;
    double shownHealth_ = 1;  // the bar's fraction, catching up with the real one (GCtrlHealthBar_v3)
    float mouseX_ = -1, mouseY_ = -1;
    uint64_t hoverSince_ = 0;
    // Pad focus
    bool pad_ = false;
    Focus focus_;
    float stickX_ = 0, stickY_ = 0;
    uint64_t stickNext_ = 0;
    // The description popup, rebuilt when its text changes.
    std::string tipText_;
    Texture tip_{};
    SDL_FRect tipRect_{};
    Texture money_{};
    uint32_t moneyShown_ = ~0u;
};

}  // namespace harbinger::app
