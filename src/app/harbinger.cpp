// harbinger: the rebuilt game. It boots to the front end: the main menu
// (startup.itf), the character screen (new.itf), Options and Load Game, drawn
// from the original interface, font, item and cursor files, with the menu
// music and UI sounds. Play opens the first level with the chosen character
// standing on its entry point, ready to walk.
//
//   harbinger [--data <install dir>] [--screen startup|new|options|load|game] [--level id]
//             [--character human|gladiator|culibine] [--no-audio] [--settings config.cfg] [--run-ms ms]
//             [--screenshot out.bmp [--time ms] [--mouse x,y] [--click x,y] [--stick x,y] [--attack]]
//
// Mouse, keyboard (up/down to move, left/right for sliders, Enter, Esc = back)
// and gamepad (D-pad/stick, A, B = back) all drive the menus. In the level the
// left stick, arrows or WASD walk the character in that direction (as in
// Diablo II Resurrected on a controller), and clicking or holding the mouse
// button walks it to the pointer, as in the original. The camera follows.
// Clicking an enemy goes and hits it (the original's GAttackAction); F, the
// pad's X / Square or the right trigger hit the nearest enemy ahead.
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
#include "app/screen.hpp"
#include "app/sound.hpp"
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
    bool audio = true;
    bool attack = false;  // screenshot mode: the attack button at 0 ms
    uint64_t timeMs = 0, runMs = 0;
    std::optional<SDL_FPoint> mouse, click, stick;  // screenshot mode: pointer, a click at 0 ms, a held stick
    world::Character character = world::Character::Human;
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
        SDL_SetWindowFullscreen(win, true);  // borderless, at the desktop's resolution
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
        character_ = opt.character;
        if (opt.screen == "new") openNew();
        else if (opt.screen == "options") openOptions();
        else if (opt.screen == "load") openLoad();
        else open(kStartupItf);
        if (opt.screen == "game") startGame();
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
                    if (current_ == kOptionsItf) showDisplay(display_);
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
            SDL_SetRenderDrawColor(ren_, 160, 255, 160, 255);
            const std::string title = world_->level().map().name + "  (walking and melee only: no HUD, scripts or enemy AI yet)";
            SDL_RenderDebugText(ren_, 8, 8, title.c_str());
            SDL_RenderDebugText(ren_, 8, 20, "Walk: stick / WASD / click   Hit: click enemy / F / X / RT   Menu: Esc / B");
        } else {
            screen_->draw(ren_);
        }
        if (showCursor_) cursor_.draw(ren_, mouse_.x, mouse_.y, now_);
    }

    void setMouse(SDL_FPoint p) {
        mouse_ = p;
        showCursor_ = true;
        screen_->pointerMove(p.x, p.y);
    }
    // Screenshot mode: walk to a view pixel, or hold the stick in a direction.
    void clickWorld(SDL_FPoint p) {
        if (world_ && !world_->attackAt(p.x, p.y)) world_->walkTo(p.x, p.y);
    }
    void attackWorld() {
        if (world_) world_->attackNearest();
    }
    void holdStick(SDL_FPoint v) { stick_ = v; }

private:
    // The game runs at 1024x768 with High Resolution on, 800x600 without
    // (Harbinger.ini [Video] HiRes; the front end is always 800x600).
    void startGame() {
        const int w = highRes_ ? 1024 : kScreenW, h = highRes_ ? 768 : kScreenH;
        world_ = std::make_unique<WorldView>(assets_, ren_, level_, character_, w, h);
        SDL_SetRenderLogicalPresentation(ren_, w, h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        gameW_ = w, gameH_ = h;
        mouse_ = {w / 2.0f, h / 2.0f};
        SDL_Log("level %u: %s (%s)", level_, world_->level().map().name.c_str(), world_->level().path().c_str());
        // The level's music (FUN_00497cd0 -> FUN_005056c0 with the header's music id).
        if (sound_) playMusic(assets_.soundPath(world_->level().map().music));
    }
    void endGame() {
        world_.reset();
        if (sound_) playMusic(introMusic_ ? &*introMusic_ : nullptr);
        SDL_SetRenderLogicalPresentation(ren_, kScreenW, kScreenH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        mouse_ = {kScreenW / 2.0f, kScreenH / 2.0f};
        open(kStartupItf);
    }

    void handleWorld(const SDL_Event& e) {
        switch (e.type) {
            case SDL_EVENT_QUIT: running_ = false; break;
            case SDL_EVENT_GAMEPAD_ADDED: SDL_OpenGamepad(e.gdevice.which); break;
            case SDL_EVENT_MOUSE_MOTION:
                mouse_ = {e.motion.x, e.motion.y};
                showCursor_ = true;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                // Swap Mouse Buttons makes the right button the one that walks.
                if (e.button.button == (settings_.swapMouse ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT)) {
                    mouse_ = {e.button.x, e.button.y};
                    showCursor_ = true;
                    // On an enemy: go and hit it (GAttackAction); elsewhere walk.
                    mouseHeld_ = !world_->attackAt(mouse_.x, mouse_.y);
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
                    endGame();
                } else if (e.key.key == SDLK_F && !e.key.repeat) {
                    world_->attackNearest();
                }
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) endGame();
                // Attack (rebuild controls, D3-style): X / Square, or the right trigger.
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_WEST) world_->attackNearest();
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) stick_.x = e.gaxis.value / 32767.0f;
                if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) stick_.y = e.gaxis.value / 32767.0f;
                if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX || e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY)
                    showCursor_ = false;  // playing on the pad: hide the pointer
                if (e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                    const bool pulled = e.gaxis.value > 16000;
                    if (pulled && !trigger_) world_->attackNearest();
                    trigger_ = pulled;
                }
                break;
            default: break;
        }
    }

    void updateWorld(uint64_t nowMs) {
        // The stick, with a radial dead zone (rebuild choice: 25% of the
        // throw), or the keys, give a screen direction to walk in.
        float dx = 0, dy = 0;
        if (stick_.x * stick_.x + stick_.y * stick_.y > kStickDeadZone * kStickDeadZone) dx = stick_.x, dy = stick_.y;
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
        } else if (mouseHeld_ && now_ - lastRetarget_ >= kRetargetMs) {
            // Holding the button keeps walking towards the pointer.
            world_->walkTo(mouse_.x, mouse_.y);
            lastRetarget_ = now_;
        }
        world_->update(nowMs);
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
        // A rebuild addition: the display choice, in the free row above the
        // buttons, in the labels' font. Action 6 is unused by the original.
        displayChoice_ = screen_->addChoice(170, 388, 460, 10, 6);
        int maxW = 0, maxH = 0;
        SDL_Rect usable;
        if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(win_), &usable)) maxW = usable.w, maxH = usable.h;
        choices_ = displayChoices(maxW, maxH);
        choice_ = 0;
        for (size_t i = 0; i < choices_.size(); ++i)
            if (choices_[i] == display_) choice_ = i;
        if (!(choices_[choice_] == display_)) {  // a size from the file that is not offered: offer it too
            choices_.insert(choices_.end() - 1, display_);
            choice_ = choices_.size() - 2;
        }
        showDisplay(display_);
    }

    void showDisplay(const Display& d) {
        if (current_ == kOptionsItf) screen_->setText(displayChoice_, "Display    -  " + d.describe() + "  +");
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
            case 6: {  // display choice (rebuild): shown now, applied on Accept
                const int n = int(choices_.size());
                choice_ = size_t((int(choice_) + screen_->choiceStep(displayChoice_) + n) % n);
                showDisplay(choices_[choice_]);
                return;
            }
            case -2:  // Accept: apply the toggles, write config.cfg
                if (!(choices_[choice_] == display_)) {
                    display_ = choices_[choice_];
                    applyDisplay(win_, display_);
                    saveDisplay();
                }
                if (swapChanged_) settings_.swapMouse = !settings_.swapMouse;
                if (highResChanged_) highRes_ = !highRes_;  // no other resolution to switch to yet
                saveSettings();
                pending_ = kStartupItf;
                return;
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
    size_t choice_ = 0, displayChoice_ = 0;
    bool swapChanged_ = false, highResChanged_ = false, highRes_ = true;
    SDL_FPoint mouse_{kScreenW / 2.0f, kScreenH / 2.0f};
    std::unique_ptr<WorldView> world_;
    std::optional<std::string> introMusic_;
    uint16_t level_ = 1;
    int gameW_ = kScreenW, gameH_ = kScreenH;
    SDL_FPoint stick_{0, 0};
    world::Character character_ = world::Character::Human;
    bool mouseHeld_ = false, steering_ = false, trigger_ = false;
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
        if (i + 1 >= argc) throw std::runtime_error("missing value for " + a);
        const std::string v = argv[++i];
        if (a == "--data") o.data = v;
        else if (a == "--screen") o.screen = v;
        else if (a == "--level") o.level = uint16_t(std::stoul(v));
        else if (a == "--screenshot") o.screenshot = v;
        else if (a == "--settings") o.settings = v;
        else if (a == "--time") o.timeMs = std::stoull(v);
        else if (a == "--run-ms") o.runMs = std::stoull(v);
        else if (a == "--character") {
            if (v == "human") o.character = world::Character::Human;
            else if (v == "gladiator") o.character = world::Character::Gladiator;
            else if (v == "culibine") o.character = world::Character::Culibine;
            else throw std::runtime_error("--character expects human, gladiator or culibine");
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
                                                 "harbinger --data <Harbinger install dir> [--screen startup|new|options|load|game] [--level id] [--no-audio] "
                                                 "[--character human|gladiator|culibine] [--settings config.cfg] "
                                                 "[--screenshot out.bmp [--time ms] [--mouse x,y] [--click x,y] [--stick x,y] [--attack]]");
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
        const Display display = loadDisplay(displayPath);
        SDL_Window* win = nullptr;
        SDL_Renderer* ren = nullptr;
        if (!SDL_CreateWindowAndRenderer("Harbinger", shot ? kScreenW : display.width, shot ? kScreenH : display.height,
                                         shot ? 0 : SDL_WINDOW_RESIZABLE, &win, &ren))
            throw std::runtime_error(SDL_GetError());
        if (display.fullscreen && !shot) applyDisplay(win, display);
        SDL_SetRenderLogicalPresentation(ren, kScreenW, kScreenH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_SetRenderVSync(ren, 1);
        if (!shot) SDL_HideCursor();

        {
            FrontEnd game(win, ren, opt, display, displayPath);
            if (shot) {
                // Deterministic capture: simulate `--time` ms in 10 ms steps.
                if (opt.mouse) game.setMouse(*opt.mouse);
                if (opt.stick) game.holdStick(*opt.stick);
                for (uint64_t t = 0; t <= opt.timeMs; t += 10) {
                    game.update(t);
                    if (t == 0 && opt.click) game.clickWorld(*opt.click);
                    if (t == 0 && opt.attack) game.attackWorld();
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
