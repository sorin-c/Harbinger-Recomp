// The in-game HUD: the character's left panel (health bar, warning light,
// health packs), right panel (energy, the CHAR and MAP buttons), the log
// button, and the inventory screen the CHAR button opens (equipment slots
// with their mod chip sockets, the item grid, money, item descriptions).
// Drawn from the character's .itf files, placed the way FUN_004c4af0 places
// each panel kind. The rebuild adds gamepad navigation of the inventory in
// the style of Diablo II: Resurrected / Diablo III.
// Notes and evidence: docs/inventory.md, docs/stats.md
#pragma once

#include <SDL3/SDL.h>

#include <optional>
#include <utility>
#include <string>
#include <vector>

#include "app/assets.hpp"
#include "app/screen.hpp"
#include "formats/itf.hpp"
#include "world/inventory.hpp"
#include "world/item_catalog.hpp"
#include "world/player.hpp"
#include "world/stats.hpp"

namespace harbinger::app {

// Where a panel's background goes on a screenW x screenH game screen, from
// its 800x600 place and its interface kind (FUN_004c4af0).
SDL_FPoint panelPosition(uint16_t kind, int x, int y, int screenW, int screenH);

class Hud {
public:
    // What the HUD shows of the player, as fractions of the maximum.
    struct Vitals {
        double hitPoints = 1, maxHitPoints = 1;
        double energy = 1, maxEnergy = 1;
    };
    // What the stats panel shows: the player's stats, the equipment's bonus
    // per skill and the name typed on the character screen.
    struct StatsView {
        world::Stats stats;
        int bonus[world::kSkillCount] = {};
        std::string name = "Player";
    };
    // StatsSeen: the stats panel was opened (the player has seen the new
    // level, GCtrlStatsThreeStateBtn_v7 sets plug+0x214).
    enum class Action { UseHealthPack, Map, QuestLog, StatsSeen };

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
    // Closing puts a held item back in the grid first; false if it has no
    // room. Closing also closes the stats panel; opening it while a new
    // level is unseen opens the stats panel too (GCtrlInvThreeStateBtn_v7).
    bool setInventoryOpen(bool open);
    bool toggleInventory() { return setInventoryOpen(!open_); }
    // The stats panel (stats.itf), opened from the inventory's STATS button.
    bool statsOpen() const { return statsOpen_; }
    void setStatsOpen(bool open);
    void toggleStats() { setStatsOpen(!statsOpen_); }
    // The screen point the world view centres on: the middle of the part
    // the inventory and the stats panel leave uncovered.
    double viewCentreX() const;
    double viewCentreY() const;

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

    // Pad on the stats panel (rebuild): up and down pick a skill, A spends
    // a point on it.
    void padStatsMove(int dy);
    void padStatsPress();

    std::vector<Action> takeActions();
    // The skills a plus button was pressed for since the last call.
    std::vector<world::Skill> takeSpentPoints() { return std::exchange(spent_, {}); }
    // A message for the message line, shown for `durationMs`.
    void notify(const std::string& text, uint64_t durationMs);
    void update(uint64_t nowMs, const Vitals& vitals, const StatsView* stats = nullptr);
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
        Texture ownBackground{};  // the stats panel's background with its see-through entry, owned
        std::vector<Control> controls;  // drawn in this order
        bool hit(float px, float py) const;
    };
    // A place the pad focus can rest on: an equipment slot or a grid cell.
    struct Focus {
        int slot = -1;
        int cx = 0, cy = 0;
    };

    void load(Panel& p, const std::string& path, bool ownPalette = false);
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
    void drawStats(SDL_Renderer* ren) const;
    void drawNotices(SDL_Renderer* ren) const;
    bool clickStats(float x, float y);
    static bool inside(const Control& c, float x, float y) {
        return x >= c.rect.x && x < c.rect.x + c.rect.w && y >= c.rect.y && y < c.rect.y + c.rect.h;
    }
    // The stats panel's text for control `c`, and its tooltip.
    std::string statsText(const Control& c) const;
    std::string statsTip(const Control& c) const;
    std::optional<world::Skill> skillOf(const Control& c) const;
    const Texture* itemTexture(uint16_t id, SDL_FRect& src) const;

    Assets& assets_;
    world::Character character_;
    int screenW_, screenH_;
    Screen::SoundFn sound_;
    Panel left_, right_, log_, inv_, popup_, stats_;
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
    // The stats panel.
    bool statsOpen_ = false;
    std::optional<StatsView> statsView_;
    std::vector<world::Skill> spent_;
    int statsFocus_ = 0;  // pad: the row of the skill A raises
    int pressedPlus_ = -1;
    uint64_t pressedUntil_ = 0;
    // Rendered text boxes by control, rebuilt when their text changes.
    mutable std::vector<std::pair<std::string, Texture>> statsText_;
    // The message line.
    struct Notice {
        std::string text;
        uint64_t until = 0;
        Texture tex{};
    };
    mutable std::vector<Notice> notices_;  // textures made on first draw
};

}  // namespace harbinger::app
