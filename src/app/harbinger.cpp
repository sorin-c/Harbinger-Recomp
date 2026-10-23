// harbinger: the rebuilt game. For now it boots to the front end: the main menu
// (startup.itf) and the character screen (new.itf), drawn from the original
// interface, font, item and cursor files, with the menu music and UI sounds.
//
//   harbinger --data <install dir> [--screen startup|new] [--no-audio] [--run-ms ms]
//             [--screenshot out.bmp [--time ms] [--mouse x,y]]
//
// Mouse, keyboard (arrows + Enter, Esc = back) and gamepad (D-pad/stick + A,
// B = back) all drive the menu.
#include <SDL3/SDL.h>

#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "app/assets.hpp"
#include "app/screen.hpp"
#include "app/sound.hpp"

using namespace harbinger;
using namespace harbinger::app;

namespace {

constexpr int kScreenW = 800, kScreenH = 600;  // the original's front-end resolution
constexpr const char* kStartupItf = "res/common/interface/start/startup.itf";
constexpr const char* kNewItf = "res/common/interface/start/new.itf";

struct Options {
    std::string data, screen = "startup", screenshot;
    bool audio = true;
    uint64_t timeMs = 0, runMs = 0;
    std::optional<SDL_FPoint> mouse;
};

class FrontEnd {
public:
    FrontEnd(SDL_Renderer* ren, const Options& opt) : ren_(ren), assets_(ren, opt.data), cursor_(assets_) {
        if (opt.audio) {
            sound_ = std::make_unique<SoundSystem>();
            // The front end's music is [Paths] IntroMusic in Harbinger.ini (FUN_004a63c0).
            if (auto music = assets_.ini().get("Paths", "IntroMusic")) {
                try {
                    sound_->playMusic(std::make_unique<audio::Mp3Stream>(assets_.read(*music)), true);
                } catch (const std::exception& ex) {
                    SDL_Log("music: %s", ex.what());
                }
            }
        }
        open(opt.screen == "new" ? kNewItf : kStartupItf);
    }

    bool running() const { return running_; }

    void handle(const SDL_Event& e) {
        switch (e.type) {
            case SDL_EVENT_QUIT: running_ = false; break;
            case SDL_EVENT_GAMEPAD_ADDED: SDL_OpenGamepad(e.gdevice.which); break;
            case SDL_EVENT_MOUSE_MOTION:
                mouse_ = {e.motion.x, e.motion.y};
                showCursor_ = true;
                screen_->pointer(mouse_.x, mouse_.y, false);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button == SDL_BUTTON_LEFT) act(screen_->pointer(e.button.x, e.button.y, true));
                break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.key == SDLK_UP || e.key.key == SDLK_LEFT) focus(-1);
                else if (e.key.key == SDLK_DOWN || e.key.key == SDLK_RIGHT || e.key.key == SDLK_TAB) focus(1);
                else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) act(screen_->activateFocus());
                else if (e.key.key == SDLK_ESCAPE) back();
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                switch (e.gbutton.button) {
                    case SDL_GAMEPAD_BUTTON_DPAD_UP:
                    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: focus(-1); break;
                    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: focus(1); break;
                    case SDL_GAMEPAD_BUTTON_SOUTH: act(screen_->activateFocus()); break;
                    case SDL_GAMEPAD_BUTTON_EAST: back(); break;
                    default: break;
                }
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY || e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
                    const int dir = e.gaxis.value > 16000 ? 1 : e.gaxis.value < -16000 ? -1 : 0;
                    if (dir && !stickHeld_) focus(dir);
                    stickHeld_ = dir != 0;
                }
                break;
            default: break;
        }
    }

    void update(uint64_t nowMs) {
        now_ = nowMs;
        if (pending_) {
            open(*pending_);
            pending_.reset();
        }
        screen_->update(nowMs);
    }

    void draw() {
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
        SDL_RenderClear(ren_);
        screen_->draw(ren_);
        if (showCursor_) cursor_.draw(ren_, mouse_.x, mouse_.y, now_);
    }

    void setMouse(SDL_FPoint p) {
        mouse_ = p;
        showCursor_ = true;
        screen_->pointer(p.x, p.y, false);
    }

private:
    void open(const std::string& itf) {
        current_ = itf;
        screen_.reset();
        screen_ = std::make_unique<Screen>(assets_, itf, [this](int slot, uint16_t id) {
            if (!sound_) return;
            if (const audio::Pcm* pcm = assets_.sound(id)) sound_->play(slot, *pcm);
        });
    }
    void focus(int delta) {
        showCursor_ = false;
        screen_->moveFocus(delta);
    }
    void back() {
        if (current_ == kNewItf) pending_ = kStartupItf;
        else running_ = false;
    }
    // Action ids come from the .itf files (AnimatedBtn / Button fields).
    void act(long action) {
        if (action < 0) return;
        if (current_ == kStartupItf) {
            switch (action) {
                case 4: pending_ = kNewItf; break;  // New Game
                case 3: running_ = false; break;    // Exit
                case 0: SDL_Log("Load Game: not rebuilt yet"); break;
                case 2: SDL_Log("Options: not rebuilt yet"); break;
                default: break;
            }
        } else if (current_ == kNewItf) {
            switch (action) {
                case 1: pending_ = kStartupItf; break;  // Back
                case 0: SDL_Log("Play: the game itself is not rebuilt yet"); break;
                case 2: case 3: case 4: screen_->select(uint32_t(action)); break;  // Human / Gladiator / Culibine
                default: break;
            }
        }
    }

    SDL_Renderer* ren_;
    Assets assets_;
    Cursor cursor_;
    std::unique_ptr<SoundSystem> sound_;
    std::unique_ptr<Screen> screen_;
    std::string current_;
    std::optional<std::string> pending_;
    SDL_FPoint mouse_{kScreenW / 2.0f, kScreenH / 2.0f};
    bool showCursor_ = false, running_ = true, stickHeld_ = false;
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
        if (i + 1 >= argc) throw std::runtime_error("missing value for " + a);
        const std::string v = argv[++i];
        if (a == "--data") o.data = v;
        else if (a == "--screen") o.screen = v;
        else if (a == "--screenshot") o.screenshot = v;
        else if (a == "--time") o.timeMs = std::stoull(v);
        else if (a == "--run-ms") o.runMs = std::stoull(v);
        else if (a == "--mouse") {
            const auto comma = v.find(',');
            if (comma == std::string::npos) throw std::runtime_error("--mouse expects x,y");
            o.mouse = SDL_FPoint{std::stof(v.substr(0, comma)), std::stof(v.substr(comma + 1))};
        }
    }
    if (o.data.empty()) throw std::runtime_error("usage: harbinger --data <Harbinger install dir> [--screen startup|new] [--no-audio] "
                                                 "[--screenshot out.bmp [--time ms] [--mouse x,y]]");
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
        SDL_Window* win = nullptr;
        SDL_Renderer* ren = nullptr;
        if (!SDL_CreateWindowAndRenderer("Harbinger", kScreenW, kScreenH,
                                         shot ? 0 : SDL_WINDOW_RESIZABLE, &win, &ren))
            throw std::runtime_error(SDL_GetError());
        SDL_SetRenderLogicalPresentation(ren, kScreenW, kScreenH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_SetRenderVSync(ren, 1);
        if (!shot) SDL_HideCursor();

        {
            FrontEnd game(ren, opt);
            if (shot) {
                // Deterministic capture: simulate `--time` ms in 10 ms steps.
                if (opt.mouse) game.setMouse(*opt.mouse);
                for (uint64_t t = 0; t <= opt.timeMs; t += 10) game.update(t);
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
        return 1;
    }
}
