// harbinger: the rebuilt game. It boots to the front end: the main menu
// (startup.itf), the character screen (new.itf), Options and Load Game, drawn
// from the original interface, font, item and cursor files, with the menu
// music and UI sounds. Play opens the first level with the chosen character
// standing on its entry point, ready to walk.
//
//   harbinger [--data <install dir>] [--screen startup|new|options|load|game] [--level id] [--entry id] [--at x,y]
//             [--character human|gladiator|culibine] [--no-audio] [--settings config.cfg] [--run-ms ms]
//             [--aspect 4:3|16:9] [--size WxH]
//             [--screenshot out.bmp [--time ms] [--mouse x,y] [--click x,y] [--stick x,y] [--attack] [--shoot] [--use]
//                                   [--inventory] [--pad udlraxyb] [--travel line]] [--codes level:entry,...] [--xp n]
//
// Mouse, keyboard (up/down to move, left/right for sliders, Enter, Esc = back)
// and gamepad (D-pad/stick, A, B = back) all drive the menus. In the level the
// left stick, arrows or WASD walk the character in that direction (as in
// Diablo II Resurrected on a controller), and clicking or holding the mouse
// button walks it to the pointer, as in the original. The camera follows.
// Clicking an enemy goes and hits it (the original's GAttackAction); F, the
// pad's X / Square or the right trigger hit the nearest enemy ahead. The
// other mouse button on an enemy shoots it with the ranged weapon; G, the
// pad's Y / Triangle or the left trigger shoot the nearest one.
//
// The HUD's CHAR button, I, C or Tab, or the pad's View / Back button open
// the inventory. On the pad (as in Diablo III on a console) the D-pad or
// the stick move the focus over the slots and grid, A picks up and puts
// down, X equips or takes off, B closes. Y opens the stats panel (the
// inventory's STATS button), where the D-pad picks a skill, A spends a bonus
// point on it and B goes back to the inventory. Q, a click on the health
// button or the D-pad's up uses a health pack. --xp gives the player
// experience at the start (a testing aid).
//
// Options adds two rows to the original's (rebuild): the display size
// (windows and fullscreen modes; type digits on the row for any other size)
// and the aspect, 4:3 or 16:9 (widescreen: the game view widens to 1366x768).
// --aspect and --size set them for one run.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "app/assets.hpp"
#include "app/dialogue.hpp"
#include "app/hud.hpp"
#include "app/screen.hpp"
#include "app/sound.hpp"
#include "app/level_select.hpp"
#include "app/world_view.hpp"
#include "engine/display.hpp"
#include "engine/settings.hpp"

using namespace harbinger;
using namespace harbinger::app;

namespace {

constexpr int kScreenW = 800, kScreenH = 600;  // the original's front-end resolution
constexpr const char* kStartupItf = "res/common/interface/start/startup.itf";
constexpr const char* kNewItf = "res/common/interface/start/new.itf";
constexpr const char* kOptionsItf = "res/common/interface/start/options.itf";
constexpr const char* kLoadItf = "res/common/interface/start/load.itf";

struct Options {
    std::string data, screen = "startup", screenshot, settings;
    uint16_t level = 1;  // a new game starts in level 1 (FUN_00497850)
    uint16_t entry = 0;  // the level's entry point (0 = "Default"; a testing aid)
    bool audio = true;
    bool attack = false;  // screenshot mode: the attack button at 0 ms
    bool shoot = false;   // screenshot mode: the shoot button at 0 ms
    bool use = false;     // screenshot mode: the use button at 0 ms and every 1.5 s
    bool inventory = false;  // screenshot mode: the inventory open
    std::string pad;         // screenshot mode: pad presses at 0 ms (u d l r = D-pad, a = A, x = X, y = Y, b = B)
    uint32_t xp = 0;         // experience given at the start (a testing aid)
    std::optional<Aspect> aspect;            // this run's aspect, over display.ini
    std::optional<std::pair<int, int>> size;  // this run's window (or the capture's) size
    uint64_t timeMs = 0, runMs = 0;
    std::optional<SDL_FPoint> mouse, click, stick;  // screenshot mode: pointer, a click at 0 ms, a held stick
    std::optional<SDL_FPoint> at;  // the player's start, map cells (a testing aid)
    std::vector<SDL_FPoint> walk;  // screenshot mode: map points to walk to, one after another
    std::string pick;                // screenshot mode: responses to pick (digits from 1), 1 s apart while talking
    world::Character character = world::Character::Human;
    // Umbilical codes to start with, "level:entry,..." (a testing aid until
    // dialogue hands them out).
    std::vector<std::pair<uint16_t, uint16_t>> codes;
    std::optional<int> travel;  // screenshot mode: the umbilical list line to pick when it opens (-1 = Cancel)
};

std::optional<std::vector<uint8_t>> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

// The rebuild's folder for settings (%APPDATA%\harbinger-recomp\harbinger on
// Windows), with a trailing separator, or "" if there is none.
std::string prefDir() {
    char* dir = SDL_GetPrefPath("harbinger-recomp", "harbinger");
    if (!dir) return "";
    std::string out = dir;
    SDL_free(dir);
    return out;
}

Display loadDisplay(const std::string& path) {
    if (path.empty()) return {};
    const auto data = readFile(path);
    return data ? Display::parse(std::string(data->begin(), data->end())) : Display{};
}

void applyDisplay(SDL_Window* win, const Display& d) {
    if (d.fullscreen) {
        // At the desktop's resolution, borderless; or the display mode
        // nearest the chosen size (exclusive).
        SDL_DisplayMode mode;
        const bool exclusive = d.fullW && SDL_GetClosestFullscreenDisplayMode(SDL_GetDisplayForWindow(win), d.fullW,
                                                                              d.fullH, 0, true, &mode);
        if (d.fullW && !exclusive) SDL_Log("no fullscreen mode near %d x %d: %s", d.fullW, d.fullH, SDL_GetError());
        SDL_SetWindowFullscreenMode(win, exclusive ? &mode : nullptr);
        SDL_SetWindowFullscreen(win, true);
    } else {
        SDL_SetWindowFullscreen(win, false);
        SDL_SetWindowSize(win, d.width, d.height);
        SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
}

class FrontEnd {
public:
    FrontEnd(SDL_Window* win, SDL_Renderer* ren, const Options& opt, const Display& display, std::string displayPath)
        : win_(win), ren_(ren), assets_(ren, opt.data), cursor_(assets_), display_(display), displayPath_(std::move(displayPath)) {
        loadSettings(opt);
        if (opt.audio) {
            sound_ = std::make_unique<SoundSystem>();
            applyVolumes();
            // The front end's music is [Paths] IntroMusic in Harbinger.ini (FUN_004a63c0).
            introMusic_ = assets_.ini().get("Paths", "IntroMusic");
            playMusic(introMusic_ ? &*introMusic_ : nullptr);
        }
        level_ = opt.level;
        entry_ = opt.entry;
        startCodes_ = opt.codes;
        travelPick_ = opt.travel;
        if (opt.at) at_ = world::WalkMap::Point{opt.at->x, opt.at->y};
        character_ = opt.character;
        if (opt.screen == "new") openNew();
        else if (opt.screen == "options") openOptions();
        else if (opt.screen == "load") openLoad();
        else open(kStartupItf);
        if (opt.screen == "game") startGame();
        if (world_ && opt.xp) world_->awardExperience(opt.xp, true);
    }

    bool running() const { return running_; }

    void handle(const SDL_Event& e) {
        if (world_) return handleWorld(e);
        switch (e.type) {
            case SDL_EVENT_QUIT: running_ = false; break;
            case SDL_EVENT_GAMEPAD_ADDED: SDL_OpenGamepad(e.gdevice.which); break;
            case SDL_EVENT_MOUSE_MOTION:
                mouse_ = {e.motion.x, e.motion.y};
                showCursor_ = true;
                screen_->pointerMove(mouse_.x, mouse_.y);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                // Swap Mouse Buttons makes the right button the one that clicks.
                if (e.button.button == (settings_.swapMouse ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT)) {
                    showCursor_ = true;
                    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) screen_->pointerDown(e.button.x, e.button.y);
                    else screen_->pointerUp(e.button.x, e.button.y);
                }
                break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT)) {
                    // Alt+Enter: fullscreen on / off, kept for next time.
                    display_.fullscreen = !display_.fullscreen;
                    applyDisplay(win_, display_);
                    saveDisplay();
                    if (current_ == kOptionsItf) listChoices(display_);
                } else if (typeSize(e.key)) {
                } else if (e.key.key == SDLK_UP) focus(-1);
                else if (e.key.key == SDLK_DOWN || e.key.key == SDLK_TAB) focus(1);
                else if (e.key.key == SDLK_LEFT) adjust(-1);
                else if (e.key.key == SDLK_RIGHT) adjust(1);
                else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) screen_->activateFocus();
                else if (e.key.key == SDLK_ESCAPE) back();
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                switch (e.gbutton.button) {
                    case SDL_GAMEPAD_BUTTON_DPAD_UP: focus(-1); break;
                    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: focus(1); break;
                    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: adjust(-1); break;
                    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: adjust(1); break;
                    case SDL_GAMEPAD_BUTTON_SOUTH: screen_->activateFocus(); break;
                    case SDL_GAMEPAD_BUTTON_EAST: back(); break;
                    default: break;
                }
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY || e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
                    const int dir = e.gaxis.value > 16000 ? 1 : e.gaxis.value < -16000 ? -1 : 0;
                    bool& held = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY ? stickY_ : stickX_;
                    if (dir && !held) {
                        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) focus(dir);
                        else adjust(dir);
                    }
                    held = dir != 0;
                }
                break;
            default: break;
        }
        dispatch();
    }

    void update(uint64_t nowMs) {
        now_ = nowMs;
        if (world_) return updateWorld(nowMs);
        if (pending_) {
            const std::string next = *pending_;
            pending_.reset();
            if (next == kNewItf) openNew();
            else if (next == kOptionsItf) openOptions();
            else if (next == kLoadItf) openLoad();
            else open(next);
        }
        screen_->update(nowMs);
        dispatch();
    }

    void draw() {
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
        SDL_RenderClear(ren_);
        if (world_) {
            world_->draw();
            hud_->draw(ren_);
            select_->draw(ren_);
            if (world_->talking()) dialogue_->draw(ren_);
        } else {
            screen_->draw(ren_);
        }
        if (showCursor_) cursor_.draw(ren_, mouse_.x, mouse_.y, now_);
    }

    void setMouse(SDL_FPoint p) {
        mouse_ = p;
        showCursor_ = true;
        screen_->pointerMove(p.x, p.y);
        if (hud_) hud_->pointerMove(p.x, p.y);
    }
    // Screenshot mode: walk to a view pixel, or hold the stick in a direction.
    void clickWorld(SDL_FPoint p) {
        if (hud_ && hud_->pointerDown(p.x, p.y)) return;
        if (world_ && world_->clickBubble(p.x, p.y)) return;
        if (world_ && !world_->attackAt(p.x, p.y) && !world_->useAt(p.x, p.y)) world_->walkTo(p.x, p.y);
    }
    void attackWorld() {
        if (world_) world_->attackNearest();
    }
    void shootWorld() {
        if (world_) world_->shootNearest();
    }
    void useWorld() {
        if (world_ && !world_->closeBubble()) world_->useNearest();
    }
    void holdStick(SDL_FPoint v) { stick_ = v; }
    void walkWorld(SDL_FPoint p) {
        if (world_) world_->walkToCell(p.x, p.y);
    }
    bool walking() const { return world_ && (!world_->player().path().empty() || world_->cinematic()); }
    bool talking() const { return world_ && world_->talking(); }
    void pickWorld(size_t i) {
        if (talking()) pickResponse(i);
    }
    void openInventory() {
        if (hud_) hud_->setInventoryOpen(true);
    }
    void pressPad(char c) {
        if (!hud_) return;
        if (hud_->statsOpen()) {
            if (c == 'u') hud_->padStatsMove(-1);
            if (c == 'd') hud_->padStatsMove(1);
            if (c == 'a') hud_->padStatsPress();
            if (c == 'y' || c == 'b') hud_->setStatsOpen(false);
            return;
        }
        if (c == 'u') hud_->padMove(0, -1);
        if (c == 'd') hud_->padMove(0, 1);
        if (c == 'l') hud_->padMove(-1, 0);
        if (c == 'r') hud_->padMove(1, 0);
        if (c == 'a') hud_->padPress();
        if (c == 'x') hud_->padQuick();
        if (c == 'y') hud_->toggleStats();
        if (c == 'b') hud_->setStatsOpen(false);
    }

private:
    // The game runs at 1024x768 with High Resolution on, 800x600 without
    // (Harbinger.ini [Video] HiRes; the front end is always 800x600). The
    // 16:9 aspect keeps the height and widens the view (rebuild).
    void startGame() {
        const int h = highRes_ ? 768 : kScreenH, w = gameViewWidth(h, display_.aspect);
        world_ = std::make_unique<WorldView>(assets_, ren_, level_, character_, w, h,
                                             Hud::inventoryLayout(assets_, character_), entry_, at_);
        world_->setSound(sound_.get());
        hud_ = std::make_unique<Hud>(assets_, character_, w, h, [this](int slot, uint16_t id, bool restart) {
            if (!sound_) return;
            if (const audio::Pcm* pcm = assets_.sound(id)) sound_->play(slot, *pcm, restart);
        });
        hud_->attach(&world_->inventory(), &world_->catalog());
        select_ = std::make_unique<LevelSelect>(assets_, w, h);
        for (const auto& [l, e] : startCodes_) world_->unlockCode(l, e, false);
        dialogue_ = std::make_unique<Dialogue>(assets_, w, h, [this](int slot, uint16_t id, bool restart) {
            if (!sound_) return;
            if (const audio::Pcm* pcm = assets_.sound(id)) sound_->play(slot, *pcm, restart);
        });
        SDL_SetRenderLogicalPresentation(ren_, w, h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        gameW_ = w, gameH_ = h;
        mouse_ = {w / 2.0f, h / 2.0f};
        SDL_Log("level %u: %s (%s)", level_, world_->level().map().name.c_str(), world_->level().path().c_str());
        // The level's music (FUN_00497cd0 -> FUN_005056c0 with the header's music id).
        if (sound_) playMusic(assets_.soundPath(world_->level().map().music));
    }
    void endGame() {
        select_.reset();
        dialogue_.reset();
        hud_.reset();
        world_.reset();
        if (sound_) playMusic(introMusic_ ? &*introMusic_ : nullptr);
        SDL_SetRenderLogicalPresentation(ren_, kScreenW, kScreenH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        mouse_ = {kScreenW / 2.0f, kScreenH / 2.0f};
        open(kStartupItf);
    }

    // The umbilical list takes all input while it is open: the mouse, the
    // arrows and Enter / Space, Escape; on the pad the D-pad or stick, A,
    // B (rebuild).
    void handleSelect(const SDL_Event& e) {
        switch (e.type) {
            case SDL_EVENT_QUIT: running_ = false; break;
            case SDL_EVENT_MOUSE_MOTION:
                mouse_ = {e.motion.x, e.motion.y};
                showCursor_ = true;
                select_->pointerMove(mouse_.x, mouse_.y);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button != (settings_.swapMouse ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT)) break;
                mouse_ = {e.button.x, e.button.y};
                showCursor_ = true;
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) select_->pointerDown(mouse_.x, mouse_.y);
                else select_->pointerUp(mouse_.x, mouse_.y);
                break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.key == SDLK_UP || e.key.key == SDLK_W) select_->moveFocus(-1);
                else if (e.key.key == SDLK_DOWN || e.key.key == SDLK_S) select_->moveFocus(1);
                else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE || e.key.key == SDLK_E) select_->pick();
                else if (e.key.key == SDLK_ESCAPE) select_->cancel();
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                showCursor_ = false;
                switch (e.gbutton.button) {
                    case SDL_GAMEPAD_BUTTON_DPAD_UP: select_->moveFocus(-1); break;
                    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: select_->moveFocus(1); break;
                    case SDL_GAMEPAD_BUTTON_SOUTH: select_->pick(); break;
                    case SDL_GAMEPAD_BUTTON_EAST: select_->cancel(); break;
                    default: break;
                }
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
                    const int dir = e.gaxis.value > 16000 ? 1 : e.gaxis.value < -16000 ? -1 : 0;
                    if (dir && !stickY_) select_->moveFocus(dir);
                    stickY_ = dir != 0;
                }
                break;
            default: break;
        }
    }

    void handleWorld(const SDL_Event& e) {
        if (world_->gameOver() && (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                                   (e.type == SDL_EVENT_KEY_DOWN && e.key.key != SDLK_ESCAPE && !e.key.repeat))) {
            // The rebuild's game over: the level starts again (saving and
            // loading are not rebuilt).
            mouseHeld_ = false;
            return startGame();
        }
        if (world_->talking()) return handleDialogue(e);
        if (select_->isOpen()) return handleSelect(e);
        switch (e.type) {
            case SDL_EVENT_QUIT: running_ = false; break;
            case SDL_EVENT_GAMEPAD_ADDED: SDL_OpenGamepad(e.gdevice.which); break;
            case SDL_EVENT_MOUSE_MOTION:
                mouse_ = {e.motion.x, e.motion.y};
                showCursor_ = true;
                hud_->pointerMove(mouse_.x, mouse_.y);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                mouse_ = {e.button.x, e.button.y};
                showCursor_ = true;
                // The other button: over an inventory item, equip it or take it
                // off (rebuild); over the HUD, nothing; elsewhere, shoot the
                // enemy under the pointer.
                if (e.button.button == (settings_.swapMouse ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT)) {
                    if (!hud_->pointerAlt(mouse_.x, mouse_.y)) world_->shootAt(mouse_.x, mouse_.y);
                    break;
                }
                // Swap Mouse Buttons makes the right button the one that walks.
                if (e.button.button == (settings_.swapMouse ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT)) {
                    // The HUD's panels take the clicks that land on them.
                    if (hud_->pointerDown(mouse_.x, mouse_.y)) {
                        mouseHeld_ = false;
                        break;
                    }
                    // On an enemy: go and hit it (GAttackAction); on a console or
                    // chest: go and use it; elsewhere walk.
                    // A thought bubble takes the click that lands on it.
                    if (world_->clickBubble(mouse_.x, mouse_.y)) break;
                    mouseHeld_ = !world_->attackAt(mouse_.x, mouse_.y) && !world_->useAt(mouse_.x, mouse_.y);
                    if (mouseHeld_) world_->walkTo(mouse_.x, mouse_.y);
                    lastRetarget_ = now_;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button == (settings_.swapMouse ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT)) mouseHeld_ = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT)) {
                    display_.fullscreen = !display_.fullscreen;
                    applyDisplay(win_, display_);
                    saveDisplay();
                } else if (e.key.key == SDLK_ESCAPE) {
                    if (hud_->inventoryOpen()) hud_->setInventoryOpen(false);
                    else endGame();
                } else if ((e.key.key == SDLK_I || e.key.key == SDLK_C || e.key.key == SDLK_TAB) && !e.key.repeat) {
                    hud_->toggleInventory();
                } else if (e.key.key == SDLK_Q && !e.key.repeat) {
                    world_->useHealthPack();
                } else if (e.key.key == SDLK_F && !e.key.repeat) {
                    world_->attackNearest();
                } else if (e.key.key == SDLK_G && !e.key.repeat) {
                    world_->shootNearest();
                } else if (e.key.key == SDLK_E || e.key.key == SDLK_SPACE) {
                    // Close a thought bubble, or use the nearest console or chest.
                    if (!world_->closeBubble()) world_->useNearest();
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                showCursor_ = false;
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
                    hud_->toggleInventory();
                    break;
                }
                if (hud_->statsOpen()) {
                    // The stats panel has the pad while it is open (rebuild).
                    switch (e.gbutton.button) {
                        case SDL_GAMEPAD_BUTTON_DPAD_UP: hud_->padStatsMove(-1); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: hud_->padStatsMove(1); break;
                        case SDL_GAMEPAD_BUTTON_SOUTH: hud_->padStatsPress(); break;
                        case SDL_GAMEPAD_BUTTON_EAST:
                        case SDL_GAMEPAD_BUTTON_NORTH: hud_->setStatsOpen(false); break;
                        default: break;
                    }
                    break;
                }
                if (hud_->inventoryOpen()) {
                    // The inventory has the pad while it is open (D3 on a console).
                    switch (e.gbutton.button) {
                        case SDL_GAMEPAD_BUTTON_DPAD_UP: hud_->padMove(0, -1); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: hud_->padMove(0, 1); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: hud_->padMove(-1, 0); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: hud_->padMove(1, 0); break;
                        case SDL_GAMEPAD_BUTTON_SOUTH: hud_->padPress(); break;
                        case SDL_GAMEPAD_BUTTON_WEST: hud_->padQuick(); break;
                        case SDL_GAMEPAD_BUTTON_NORTH: hud_->setStatsOpen(true); break;
                        case SDL_GAMEPAD_BUTTON_EAST: hud_->setInventoryOpen(false); break;
                        default: break;
                    }
                    break;
                }
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) world_->useHealthPack();
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) endGame();
                // Attack (rebuild controls, D3-style): X / Square, or the right trigger.
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_WEST) world_->attackNearest();
                // Shoot (rebuild controls): Y / Triangle, or the left trigger.
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) world_->shootNearest();
                // Use (rebuild controls): A / Cross, the nearest console or chest.
                // It also closes a thought bubble first.
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH && !world_->closeBubble()) world_->useNearest();
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) stick_.x = e.gaxis.value / 32767.0f;
                if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) stick_.y = e.gaxis.value / 32767.0f;
                if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX || e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
                    showCursor_ = false;  // playing on the pad: hide the pointer
                    hud_->padStick(stick_.x, stick_.y);
                }
                if (e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                    const bool pulled = e.gaxis.value > 16000;
                    if (pulled && !trigger_) world_->attackNearest();
                    trigger_ = pulled;
                }
                if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) {
                    const bool pulled = e.gaxis.value > 16000;
                    if (pulled && !leftTrigger_) world_->shootNearest();
                    leftTrigger_ = pulled;
                }
                break;
            default: break;
        }
    }

    // A conversation has the mouse, keys and pad while it is open
    // (rebuild controls besides the mouse: up / down or the D-pad move
    // along the responses, Enter, Space, E or A picks, 1-4 pick directly,
    // Page Up / Down, the wheel or the shoulders scroll, Escape or B close).
    void handleDialogue(const SDL_Event& e) {
        mouseHeld_ = false;
        switch (e.type) {
            case SDL_EVENT_QUIT: running_ = false; break;
            case SDL_EVENT_GAMEPAD_ADDED: SDL_OpenGamepad(e.gdevice.which); break;
            case SDL_EVENT_MOUSE_MOTION:
                mouse_ = {e.motion.x, e.motion.y};
                showCursor_ = true;
                dialogue_->pointerMove(mouse_.x, mouse_.y);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button == (settings_.swapMouse ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT)) {
                    mouse_ = {e.button.x, e.button.y};
                    showCursor_ = true;
                    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) dialogue_->pointerDown(mouse_.x, mouse_.y);
                    else dialogue_->pointerUp(mouse_.x, mouse_.y);
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL: dialogue_->wheel(e.wheel.y > 0 ? -1 : e.wheel.y < 0 ? 1 : 0); break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.key == SDLK_ESCAPE) world_->story().close();
                else if (e.key.key == SDLK_UP || e.key.key == SDLK_W) dialogue_->moveFocus(-1);
                else if (e.key.key == SDLK_DOWN || e.key.key == SDLK_S || e.key.key == SDLK_TAB) dialogue_->moveFocus(1);
                else if (e.key.key == SDLK_PAGEUP) dialogue_->scroll(-4);
                else if (e.key.key == SDLK_PAGEDOWN) dialogue_->scroll(4);
                else if ((e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE || e.key.key == SDLK_E) && !e.key.repeat)
                    dialogue_->activateFocus();
                else if (e.key.key >= SDLK_1 && e.key.key <= SDLK_4 && !e.key.repeat)
                    pickResponse(size_t(e.key.key - SDLK_1));
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                showCursor_ = false;
                switch (e.gbutton.button) {
                    case SDL_GAMEPAD_BUTTON_DPAD_UP: dialogue_->moveFocus(-1); break;
                    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: dialogue_->moveFocus(1); break;
                    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: dialogue_->scroll(-4); break;
                    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: dialogue_->scroll(4); break;
                    case SDL_GAMEPAD_BUTTON_SOUTH: dialogue_->activateFocus(); break;
                    case SDL_GAMEPAD_BUTTON_EAST: world_->story().close(); break;
                    default: break;
                }
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) stick_.x = e.gaxis.value / 32767.0f;
                if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
                    stick_.y = e.gaxis.value / 32767.0f;
                    // The stick steps the focus once per push.
                    const int dir = e.gaxis.value > 16000 ? 1 : e.gaxis.value < -16000 ? -1 : 0;
                    if (dir && !dialogueStick_) dialogue_->moveFocus(dir);
                    dialogueStick_ = dir != 0;
                }
                break;
            default: break;
        }
    }
    void pickResponse(size_t i) {
        if (i < world_->story().responses().size()) world_->story().choose(i);
    }

    void updateWorld(uint64_t nowMs) {
        if (world_->talking()) {
            // The conversation: the world stands still (WorldView::update).
            dialogue_->sync(world_->story());
            dialogue_->update(nowMs);
            if (const auto i = dialogue_->takeChoice()) pickResponse(*i);
            if (world_->talking()) dialogue_->sync(world_->story());
            if (steering_) world_->steer(0, 0), steering_ = false;
            world_->update(nowMs);
            const world::Player& p = world_->player();
            hud_->update(nowMs, Hud::Vitals{p.hitPoints(), p.maxHitPoints(), p.energy(), p.maxEnergy()});
            return;
        }
        // The stick, with a radial dead zone (rebuild choice: 25% of the
        // throw), or the keys, give a screen direction to walk in.
        float dx = 0, dy = 0;
        // While the inventory is open the stick moves its focus instead.
        if (!hud_->inventoryOpen() && stick_.x * stick_.x + stick_.y * stick_.y > kStickDeadZone * kStickDeadZone)
            dx = stick_.x, dy = stick_.y;
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) dx -= 1;
        if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) dx += 1;
        if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) dy -= 1;
        if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) dy += 1;
        if (dx != 0 || dy != 0) {
            world_->steer(dx, dy);
            steering_ = true;
        } else if (steering_) {
            world_->steer(0, 0);
            steering_ = false;
        } else if (mouseHeld_ && !world_->usePending() && now_ - lastRetarget_ >= kRetargetMs) {
            // Holding the button keeps walking towards the pointer.
            world_->walkTo(mouse_.x, mouse_.y);
            lastRetarget_ = now_;
        }
        for (const Hud::Action a : hud_->takeActions()) {
            if (a == Hud::Action::UseHealthPack) world_->useHealthPack();  // the map and quest log are not rebuilt yet
            if (a == Hud::Action::StatsSeen) world_->setLevelSeen();
        }
        for (const world::Skill s : hud_->takeSpentPoints()) world_->spendPoint(s);
        world_->setViewCentre(hud_->viewCentreX(), hud_->viewCentreY());
        world_->update(nowMs);
        // The umbilical's list: open it when the shell has closed, hand the
        // pick back.
        if (const auto* menu = world_->travelMenu()) {
            if (!select_->isOpen()) {
                select_->open(world::UmbilicalCodes::listText(*menu));
                if (travelPick_) select_->moveFocus(1);
            }
            if (travelPick_) {
                // Screenshot mode: pick the asked line (or cancel) at once.
                const int pick = *travelPick_;
                travelPick_.reset();
                world_->chooseTravel(pick);
                select_->close();
            } else if (const auto choice = select_->takeChoice()) {
                world_->chooseTravel(*choice);
                select_->close();
            }
        }
        if (const auto change = world_->levelChange()) {
            // A portal, an umbilical or SetLevel: the next level, with the
            // player on its entry point (FUN_004a1d20 sets the load going),
            // carrying what it has. SetLevel names no entry point
            // (0xffff); the rebuild uses the Default one.
            auto carried = world_->takeCarried();
            level_ = change->level;
            entry_ = change->entry == Cutscene::kNoEntry ? 0 : change->entry;
            at_.reset();
            startCodes_.clear();
            startGame();
            world_->carry(std::move(carried));
            if (change->umbilical) world_->arriveByUmbilical();
            hud_->attach(&world_->inventory(), &world_->catalog());
            return;
        }
        const world::Player& p = world_->player();
        Hud::StatsView stats{p.stats()};
        for (int i = 0; i < world::kSkillCount; ++i) stats.bonus[i] = p.skillBonus(world::Skill(i));
        stats.name = playerName_;
        hud_->update(nowMs, Hud::Vitals{p.hitPoints(), p.maxHitPoints(), p.energy(), p.maxEnergy()}, &stats);
    }

    void playMusic(const std::string* path) {
        if (!path) return sound_->stopMusic();
        try {
            sound_->playMusic(std::make_unique<audio::Mp3Stream>(assets_.read(*path)), true);
        } catch (const std::exception& ex) {
            SDL_Log("music %s: %s", path->c_str(), ex.what());
            sound_->stopMusic();
        }
    }

    void open(const std::string& itf) {
        current_ = itf;
        screen_.reset();
        screen_ = std::make_unique<Screen>(assets_, itf, [this](int slot, uint16_t id, bool restart) {
            if (!sound_) return;
            if (const audio::Pcm* pcm = assets_.sound(id)) sound_->play(slot, *pcm, restart);
        });
    }

    // Character choice (interface 0x17): Play stays disabled until a
    // character is picked, and the name box starts as "Player" (both seen in
    // the original).
    enum NewControl : size_t { kPlay = 1, kPlayerName = 6 };
    void openNew() {
        open(kNewItf);
        screen_->setDisabled(kPlay, true);
        screen_->setText(kPlayerName, "Player");
    }

    // Options (interface 0x15). FUN_004c9180 hands the controls their values
    // and action ids by .itf index: Swap Mouse 6, the sliders 8-10, High
    // Resolution 14.
    enum OptionsControl : size_t { kSwapMouse = 6, kMusic = 8, kSound = 9, kGamma = 10, kHighRes = 14 };
    void openOptions() {
        open(kOptionsItf);
        saved_ = settings_;
        swapChanged_ = highResChanged_ = false;
        screen_->setCheck(kSwapMouse, 1, settings_.swapMouse);
        screen_->setSlider(kMusic, 2, settings_.music);
        screen_->setSlider(kSound, 3, settings_.sound);
        screen_->setSlider(kGamma, 4, settings_.gamma);
        screen_->setCheck(kHighRes, 5, highRes_);
        // Rebuild additions, in the free row above the buttons, in the
        // labels' font: the display size and the aspect. Actions 6 and 7 are
        // unused by the original.
        displayChoice_ = screen_->addChoice(kSizeRow.x, kSizeRow.y, kSizeRow.w, 10, 6);
        aspectChoice_ = screen_->addChoice(kAspectRow.x, kAspectRow.y, kAspectRow.w, 10, 7);
        pickedAspect_ = display_.aspect;
        typed_.reset();
        listChoices(display_);
    }

    // The sizes for the picked aspect, with `current` selected (added to the
    // list when it is not offered: a custom size).
    void listChoices(const Display& current) {
        int maxW = 0, maxH = 0;
        const SDL_DisplayID screen = SDL_GetDisplayForWindow(win_);
        SDL_Rect usable;
        if (SDL_GetDisplayUsableBounds(screen, &usable)) maxW = usable.w, maxH = usable.h;
        // The display's fullscreen modes, one per size, of the picked shape.
        std::vector<std::pair<int, int>> modes;
        int count = 0;
        if (SDL_DisplayMode** list = SDL_GetFullscreenDisplayModes(screen, &count)) {
            for (int i = 0; i < count; ++i) {
                const int w = list[i]->w, h = list[i]->h;
                const bool wide = w * 9 == h * 16 || (w == 1366 && h == 768);
                if ((pickedAspect_ == Aspect::Wide) == wide && (wide || w * 3 == h * 4)) modes.emplace_back(w, h);
            }
            SDL_free(list);
        }
        choices_ = displayChoices(pickedAspect_, maxW, maxH, modes);
        // Fullscreen choices keep the window size to come back to.
        for (Display& d : choices_)
            if (d.fullscreen) d.width = current.width, d.height = current.height;
        choice_ = choices_.size();
        for (size_t i = 0; i < choices_.size(); ++i)
            if (choices_[i].sameSize(current)) choice_ = i;
        if (choice_ == choices_.size()) {
            Display d = current;
            d.aspect = pickedAspect_;
            // A window goes with the windows, a fullscreen size at the end.
            const auto at = d.fullscreen ? choices_.end()
                                         : std::find_if(choices_.begin(), choices_.end(), [](const Display& o) { return o.fullscreen; });
            choice_ = size_t(choices_.insert(at, d) - choices_.begin());
        }
        showDisplay();
    }

    void showDisplay() {
        if (current_ != kOptionsItf) return;
        if (typed_) screen_->setText(displayChoice_, "Size    " + *typed_ + "_    Enter");
        else screen_->setText(displayChoice_, "-  " + choices_[choice_].describe() + "  +");
        screen_->setText(aspectChoice_, std::string("Aspect   -  ") + describe(pickedAspect_) + "  +");
    }

    // Typing a size on the size row (rebuild): digits, then x (or a space)
    // and more digits, Enter takes it as a custom window or fullscreen size.
    // True if the key went to it.
    bool typeSize(const SDL_KeyboardEvent& k) {
        if (current_ != kOptionsItf) return false;
        const bool onRow = screen_->focused() == displayChoice_ ||
                           (showCursor_ && SDL_PointInRectFloat(&mouse_, &kSizeRow));
        if (!onRow && !typed_) return false;
        int digit = -1;
        if (k.key >= SDLK_0 && k.key <= SDLK_9) digit = int(k.key - SDLK_0);
        else if (k.key >= SDLK_KP_1 && k.key <= SDLK_KP_9) digit = int(k.key - SDLK_KP_1) + 1;
        else if (k.key == SDLK_KP_0) digit = 0;
        if (!typed_) {
            if (digit < 0) return false;
            typed_ = "";
        }
        if (k.key == SDLK_UP || k.key == SDLK_DOWN || k.key == SDLK_TAB) {  // moving away drops it
            typed_.reset();
            showDisplay();
            return false;
        }
        std::string& t = *typed_;
        if (digit >= 0 && t.size() < 11) {
            t += char('0' + digit);
        } else if ((k.key == SDLK_X || k.key == SDLK_SPACE || k.key == SDLK_KP_MULTIPLY) && !t.empty() &&
                   t.find('x') == std::string::npos) {
            t += " x ";
        } else if (k.key == SDLK_BACKSPACE) {
            if (t.size() >= 3 && t.compare(t.size() - 3, 3, " x ") == 0) t.resize(t.size() - 3);
            else if (!t.empty()) t.pop_back();
        } else if (k.key == SDLK_RETURN || k.key == SDLK_KP_ENTER) {
            int w = 0, h = 0;
            if (parseSize(t, w, h)) {
                Display d = choices_[choice_];
                if (d.fullscreen) d.fullW = w, d.fullH = h;
                else d.width = w, d.height = h;
                typed_.reset();
                listChoices(d);
            } else {
                screen_->playSlot(Screen::kHoverSound, false);  // not a size (320x240 to 16384x16384)
            }
            return true;
        } else if (k.key == SDLK_ESCAPE) {
            typed_.reset();
        } else if (digit < 0) {
            return true;  // other keys do nothing while typing
        }
        showDisplay();
        return true;
    }

    // Load Game (interface 0xd). The rebuild cannot read the original's saves
    // and has none of its own yet, so both lists stay empty.
    enum LoadControl : size_t { kSaveList = 5, kCharacterList = 10 };
    void openLoad() {
        open(kLoadItf);
        screen_->setText(kSaveList, "");
        screen_->setText(kCharacterList, "");
        for (size_t scroll : {3, 4, 8, 9}) screen_->setDisabled(scroll, true);
    }

    void focus(int delta) {
        showCursor_ = false;
        screen_->moveFocus(delta);
    }
    void adjust(int dir) {
        showCursor_ = false;
        screen_->adjustFocus(dir);
    }
    void back() {
        if (current_ == kOptionsItf) optionsAction(-3);  // Cancel
        else if (current_ != kStartupItf) pending_ = kStartupItf;
        else running_ = false;
    }

    void dispatch() {
        for (const Notice& n : screen_->takeNotices()) act(n.action);
    }

    // Action ids come from the .itf files, or from the handler (Options).
    void act(long action) {
        if (current_ == kStartupItf) {
            switch (action) {
                case 4: pending_ = kNewItf; break;      // New Game
                case 3: running_ = false; break;        // Exit
                case 0: pending_ = kLoadItf; break;     // Load Game
                case 2: pending_ = kOptionsItf; break;  // Options
                default: break;
            }
        } else if (current_ == kNewItf) {
            switch (action) {
                case 1: pending_ = kStartupItf; break;  // Back
                case 0: startGame(); return;  // Play
                case 2: case 3: case 4:  // Human / Gladiator / Culibine
                    screen_->select(uint32_t(action));
                    character_ = world::Character(action - 2);
                    screen_->setDisabled(kPlay, false);
                    break;
                default: break;
            }
        } else if (current_ == kOptionsItf) {
            optionsAction(action);
        } else if (current_ == kLoadItf) {
            switch (action) {
                case 0: pending_ = kStartupItf; break;  // Cancel
                case 1: SDL_Log("Load: no saved games"); break;
                case 2: SDL_Log("Delete: no saved games"); break;
                default: break;
            }
        }
    }

    // The Options branch of FUN_004c80c0.
    void optionsAction(long action) {
        switch (action) {
            case 0: SDL_Log("Configure Controls: not rebuilt yet"); return;
            case 1: swapChanged_ = !swapChanged_; return;
            case 5: highResChanged_ = !highResChanged_; return;
            case 6: {  // display size (rebuild): shown now, applied on Accept
                typed_.reset();
                const int n = int(choices_.size());
                choice_ = size_t((int(choice_) + screen_->choiceStep(displayChoice_) + n) % n);
                showDisplay();
                return;
            }
            case 7:  // aspect (rebuild): the sizes offered follow it
                typed_.reset();
                pickedAspect_ = pickedAspect_ == Aspect::Wide ? Aspect::Classic : Aspect::Wide;
                listChoices(choices_[choice_]);
                return;
            case -2: {  // Accept: apply the toggles, write config.cfg
                Display picked = choices_[choice_];
                picked.aspect = pickedAspect_;
                if (!picked.sameSize(display_) || picked.aspect != display_.aspect) {
                    const bool resize = !picked.sameSize(display_);
                    display_ = picked;
                    if (resize) applyDisplay(win_, display_);
                    saveDisplay();
                }
                if (swapChanged_) settings_.swapMouse = !settings_.swapMouse;
                if (highResChanged_) highRes_ = !highRes_;  // no other resolution to switch to yet
                saveSettings();
                pending_ = kStartupItf;
                return;
            }
            case -3:  // Cancel: back to the stored settings
                settings_ = saved_;
                applyVolumes();
                pending_ = kStartupItf;
                return;
            case 2: case 3: case 4: {
                // The sliders report live; the handler reads the value back (0x28).
                const double v = screen_->sliderChanged(size_t(action) + 6);
                if (v < 0) return;
                if (action == 2) settings_.music = v;
                if (action == 3) settings_.sound = v;
                if (action == 4) settings_.gamma = v;  // stored; the display gamma is not applied yet
                applyVolumes();
                if (action == 3) screen_->playSlot(Screen::kHoverSound, false);  // sound check
                return;
            }
            default: return;
        }
    }

    // The rebuild keeps its own config.cfg (same layout as the original's) in
    // the user's preference folder; the first run starts from the install's.
    void loadSettings(const Options& opt) {
        if (!opt.settings.empty()) {
            settingsPath_ = opt.settings;
        } else if (opt.screenshot.empty()) {
            if (const std::string dir = prefDir(); !dir.empty()) settingsPath_ = dir + "config.cfg";
        }
        for (const std::string& path : {settingsPath_, opt.data + "/config.cfg"}) {
            if (path.empty()) continue;
            const auto data = readFile(path);
            if (!data) continue;
            try {
                settings_ = Settings::parse(*data);
                return;
            } catch (const std::exception& ex) {
                SDL_Log("%s: %s", path.c_str(), ex.what());
            }
        }
    }
    void saveSettings() {
        if (settingsPath_.empty()) return;
        const auto bytes = settings_.serialize();
        std::ofstream f(settingsPath_, std::ios::binary | std::ios::trunc);
        if (!f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())))
            SDL_Log("could not write %s", settingsPath_.c_str());
    }
    void saveDisplay() {
        if (displayPath_.empty()) return;
        std::ofstream f(displayPath_, std::ios::trunc);
        if (!(f << display_.serialize())) SDL_Log("could not write %s", displayPath_.c_str());
    }
    void applyVolumes() {
        if (!sound_) return;
        sound_->setMusicVolume(float(settings_.musicGain()));
        sound_->setSoundVolume(float(settings_.sound));
    }

    SDL_Window* win_;
    SDL_Renderer* ren_;
    Assets assets_;
    Cursor cursor_;
    std::unique_ptr<SoundSystem> sound_;
    std::unique_ptr<Screen> screen_;
    std::string current_;
    std::optional<std::string> pending_;
    Settings settings_, saved_;
    std::string settingsPath_;
    Display display_;
    std::string displayPath_;
    std::vector<Display> choices_;
    size_t choice_ = 0, displayChoice_ = 0, aspectChoice_ = 0;
    Aspect pickedAspect_ = Aspect::Classic;
    std::optional<std::string> typed_;  // a size being typed on the Options screen
    // The Options screen's rebuild rows (800x600 pixels).
    static constexpr SDL_FRect kSizeRow{130, 388, 330, 18}, kAspectRow{460, 388, 220, 18};
    bool swapChanged_ = false, highResChanged_ = false, highRes_ = true;
    SDL_FPoint mouse_{kScreenW / 2.0f, kScreenH / 2.0f};
    std::unique_ptr<WorldView> world_;
    std::unique_ptr<Hud> hud_;
    std::unique_ptr<LevelSelect> select_;
    std::vector<std::pair<uint16_t, uint16_t>> startCodes_;
    std::optional<int> travelPick_;
    std::unique_ptr<Dialogue> dialogue_;
    std::optional<std::string> introMusic_;
    uint16_t level_ = 1, entry_ = 0;
    std::optional<world::WalkMap::Point> at_;
    int gameW_ = kScreenW, gameH_ = kScreenH;
    SDL_FPoint stick_{0, 0};
    world::Character character_ = world::Character::Human;
    std::string playerName_ = "Player";  // the character screen's name box (not editable yet)
    bool mouseHeld_ = false, steering_ = false, trigger_ = false, leftTrigger_ = false, dialogueStick_ = false;
    uint64_t lastRetarget_ = 0;
    static constexpr float kStickDeadZone = 0.25f;
    static constexpr uint64_t kRetargetMs = 100;  // rebuild choice
    bool showCursor_ = false, running_ = true, stickX_ = false, stickY_ = false;
    uint64_t now_ = 0;
};

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--no-audio") {
            o.audio = false;
            continue;
        }
        if (a == "--attack") {
            o.attack = true;
            continue;
        }
        if (a == "--shoot") {
            o.shoot = true;
            continue;
        }
        if (a == "--use") {
            o.use = true;
            continue;
        }
        if (a == "--inventory") {
            o.inventory = true;
            continue;
        }
        if (i + 1 >= argc) throw std::runtime_error("missing value for " + a);
        const std::string v = argv[++i];
        if (a == "--data") o.data = v;
        else if (a == "--screen") o.screen = v;
        else if (a == "--level") o.level = uint16_t(std::stoul(v));
        else if (a == "--entry") o.entry = uint16_t(std::stoul(v));
        else if (a == "--screenshot") o.screenshot = v;
        else if (a == "--settings") o.settings = v;
        else if (a == "--time") o.timeMs = std::stoull(v);
        else if (a == "--run-ms") o.runMs = std::stoull(v);
        else if (a == "--pad") o.pad = v;
        else if (a == "--xp") o.xp = uint32_t(std::stoul(v));
        else if (a == "--travel") o.travel = std::stoi(v);
        else if (a == "--codes") {
            // level:entry pairs, comma separated.
            size_t at = 0;
            while (at < v.size()) {
                const size_t end = std::min(v.find(',', at), v.size());
                const std::string one = v.substr(at, end - at);
                const auto colon = one.find(':');
                if (colon == std::string::npos) throw std::runtime_error("--codes expects level:entry,...");
                o.codes.emplace_back(uint16_t(std::stoul(one.substr(0, colon))), uint16_t(std::stoul(one.substr(colon + 1))));
                at = end + 1;
            }
        }
        else if (a == "--aspect") {
            if (v != "4:3" && v != "16:9") throw std::runtime_error("--aspect expects 4:3 or 16:9");
            o.aspect = v == "16:9" ? Aspect::Wide : Aspect::Classic;
        } else if (a == "--size") {
            int w = 0, h = 0;
            if (!parseSize(v, w, h)) throw std::runtime_error("--size expects WxH, from 320x240 to 16384x16384");
            o.size = std::pair{w, h};
        }
        else if (a == "--character") {
            if (v == "human") o.character = world::Character::Human;
            else if (v == "gladiator") o.character = world::Character::Gladiator;
            else if (v == "culibine") o.character = world::Character::Culibine;
            else throw std::runtime_error("--character expects human, gladiator or culibine");
        } else if (a == "--walk") {
            for (size_t at = 0; at < v.size();) {
                const size_t end = std::min(v.find(';', at), v.size()), comma = v.find(',', at);
                if (comma == std::string::npos || comma > end) throw std::runtime_error("--walk expects x,y[;x,y...]");
                o.walk.push_back({std::stof(v.substr(at, comma - at)), std::stof(v.substr(comma + 1, end - comma - 1))});
                at = end + 1;
            }
        } else if (a == "--pick") {
            o.pick = v;
        } else if (a == "--at") {
            const auto comma = v.find(',');
            if (comma == std::string::npos) throw std::runtime_error("--at expects x,y");
            o.at = SDL_FPoint{std::stof(v.substr(0, comma)), std::stof(v.substr(comma + 1))};
        } else if (a == "--click" || a == "--stick") {
            const auto comma = v.find(',');
            if (comma == std::string::npos) throw std::runtime_error(a + " expects x,y");
            (a == "--click" ? o.click : o.stick) = SDL_FPoint{std::stof(v.substr(0, comma)), std::stof(v.substr(comma + 1))};
        } else if (a == "--mouse") {
            const auto comma = v.find(',');
            if (comma == std::string::npos) throw std::runtime_error("--mouse expects x,y");
            o.mouse = SDL_FPoint{std::stof(v.substr(0, comma)), std::stof(v.substr(comma + 1))};
        }
    }
    // Without --data, look for the game next to the executable, then in the
    // current folder, so the exe can simply be dropped into the install.
    if (o.data.empty()) {
        for (const std::string& dir : {std::string(SDL_GetBasePath() ? SDL_GetBasePath() : ""), std::string(".")})
            if (!dir.empty() && std::ifstream(dir + "/Harbinger.ini")) {
                o.data = dir;
                break;
            }
    }
    if (o.data.empty()) throw std::runtime_error("Harbinger.ini not found. Put harbinger.exe in the Harbinger folder, or run:\n"
                                                 "harbinger --data <Harbinger install dir> [--screen startup|new|options|load|game] [--level id] [--entry id] [--at x,y] [--no-audio] "
                                                 "[--character human|gladiator|culibine] [--settings config.cfg] [--aspect 4:3|16:9] [--size WxH] "
                                                 "[--screenshot out.bmp [--time ms] [--mouse x,y] [--click x,y] [--stick x,y] [--attack] [--shoot] [--use] "
                                                 "[--inventory] [--pad udlraxyb] [--travel line] [--walk x,y;x,y...] [--pick 1234]] [--codes level:entry,...] [--xp n]");
    return o;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Options opt = parseArgs(argc, argv);
        const bool shot = !opt.screenshot.empty();
        if (shot) opt.audio = false;
        if (shot) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) throw std::runtime_error(SDL_GetError());
        // The window as it was left (display.ini next to the settings).
        const std::string displayPath = shot ? "" : prefDir().empty() ? "" : prefDir() + "display.ini";
        Display display = loadDisplay(displayPath);
        // --aspect and --size override it for this run (a size is a window).
        if (opt.aspect) display.aspect = *opt.aspect;
        if (opt.size) display.fullscreen = false, display.width = opt.size->first, display.height = opt.size->second;
        // A capture of the game is taken at its 1024x768 (1366x768 at 16:9),
        // of the front end at 800x600, unless --size says otherwise.
        if (shot && !opt.size) {
            const bool game = opt.screen == "game";
            display.width = game ? gameViewWidth(768, display.aspect) : kScreenW;
            display.height = game ? 768 : kScreenH;
        }
        SDL_Window* win = nullptr;
        SDL_Renderer* ren = nullptr;
        if (!SDL_CreateWindowAndRenderer("Harbinger", display.width, display.height, shot ? 0 : SDL_WINDOW_RESIZABLE,
                                         &win, &ren))
            throw std::runtime_error(SDL_GetError());
        if (display.fullscreen && !shot) applyDisplay(win, display);
        SDL_SetRenderLogicalPresentation(ren, kScreenW, kScreenH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_SetRenderVSync(ren, 1);
        if (!shot) SDL_HideCursor();

        {
            FrontEnd game(win, ren, opt, display, displayPath);
            if (shot) {
                // Deterministic capture: simulate `--time` ms in 10 ms steps.
                if (opt.inventory) game.openInventory();
                if (opt.mouse) game.setMouse(*opt.mouse);
                if (opt.stick) game.holdStick(*opt.stick);
                for (char c : opt.pad) game.pressPad(c);
                uint64_t pickAt = 0;
                size_t next = 0, walked = 0;
                for (uint64_t t = 0; t <= opt.timeMs; t += 10) {
                    game.update(t);
                    if (t == 0 && opt.click) game.clickWorld(*opt.click);
                    if (t == 0 && opt.attack) game.attackWorld();
                    if (t == 0 && opt.shoot) game.shootWorld();
                    if (t % 1500 == 0 && opt.use) game.useWorld();
                    if (walked < opt.walk.size() && !game.walking() && !game.talking())
                        game.walkWorld(opt.walk[walked++]);
                    // Each pick a second after the conversation shows the
                    // node before it.
                    if (game.talking() && pickAt == 0) pickAt = t + 1000;
                    if (pickAt && t >= pickAt && next < opt.pick.size()) {
                        game.pickWorld(size_t(opt.pick[next++] - '1'));
                        pickAt = 0;
                    }
                }
                game.draw();
                SDL_Surface* s = SDL_RenderReadPixels(ren, nullptr);
                const bool ok = s && SDL_SaveBMP(s, opt.screenshot.c_str());
                SDL_DestroySurface(s);
                if (!ok) throw std::runtime_error(SDL_GetError());
                std::printf("wrote %s\n", opt.screenshot.c_str());
            } else {
                while (game.running() && (!opt.runMs || SDL_GetTicks() < opt.runMs)) {
                    SDL_Event e;
                    while (SDL_PollEvent(&e)) {
                        SDL_ConvertEventToRenderCoordinates(ren, &e);
                        game.handle(e);
                    }
                    game.update(SDL_GetTicks());
                    game.draw();
                    SDL_RenderPresent(ren);
                }
            }
        }
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        // Double-clicked on Windows there is no console to read, so show it.
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Harbinger", ex.what(), nullptr);
        return 1;
    }
}
