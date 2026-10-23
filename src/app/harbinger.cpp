// harbinger: the rebuilt game. For now it boots to the front end: the main menu
// (startup.itf) and the character screen (new.itf), drawn from the original
// interface, font, item and cursor files.
//
//   harbinger --data <install dir> [--screen startup|new]
//             [--screenshot out.bmp [--time ms] [--mouse x,y]]
//
// Mouse, keyboard (arrows + Enter, Esc = back) and gamepad (D-pad/stick + A,
// B = back) all drive the menu.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/vfs.hpp"
#include "formats/acr.hpp"
#include "formats/fnt.hpp"
#include "formats/gim.hpp"
#include "formats/itf.hpp"
#include "formats/itm.hpp"
#include "formats/mgr.hpp"
#include "render/sprite.hpp"
#include "ui/item_animation.hpp"
#include "ui/text.hpp"

using namespace harbinger;

namespace {

constexpr int kScreenW = 800, kScreenH = 600;  // the original's front-end resolution
constexpr const char* kStartupItf = "res/common/interface/start/startup.itf";
constexpr const char* kNewItf = "res/common/interface/start/new.itf";

std::vector<uint8_t> readOrThrow(Vfs& vfs, const std::string& path) {
    auto data = vfs.read(path);
    if (!data) throw std::runtime_error("missing resource: " + path);
    return std::move(*data);
}

struct Texture {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
};

// Game data shared by every screen: the indexes, fonts and decoded images.
class Assets {
public:
    Assets(SDL_Renderer* ren, const std::string& dir) : ren_(ren), vfs_(dir) {
        fonts_ = mgr::parse(readOrThrow(vfs_, "res/common/managers/font.mgr"));
        items_ = mgr::parse(readOrThrow(vfs_, "res/common/managers/item.mgr"));
        itemImages_ = mgr::parse(readOrThrow(vfs_, "res/common/managers/itemimg.mgr"));
        cursors_ = mgr::parse(readOrThrow(vfs_, "res/common/managers/cursor.mgr"));
    }
    ~Assets() {
        for (auto& [path, t] : textures_) SDL_DestroyTexture(t.tex);
    }

    Vfs& vfs() { return vfs_; }

    const gim::Image& image(const std::string& path) {
        auto it = images_.find(path);
        if (it == images_.end()) it = images_.emplace(path, gim::decode(readOrThrow(vfs_, path))).first;
        return it->second;
    }

    // UI art: the green key colour is transparent.
    const Texture& texture(const std::string& path) {
        auto it = textures_.find(path);
        if (it != textures_.end()) return it->second;
        const gim::Image& img = image(path);
        return textures_[path] = upload(render::toRgba(img, 0, render::KeyMode::Transparent), img.width, img.height);
    }

    Texture upload(const std::vector<uint32_t>& px, int w, int h) {
        Texture t{SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h), w, h};
        if (!t.tex) throw std::runtime_error(SDL_GetError());
        SDL_UpdateTexture(t.tex, nullptr, px.data(), w * 4);
        SDL_SetTextureBlendMode(t.tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t.tex, SDL_SCALEMODE_NEAREST);
        return t;
    }

    const fnt::Font& font(uint16_t id) {
        auto it = fontCache_.find(id);
        if (it != fontCache_.end()) return it->second;
        const std::string* path = fonts_.find(id);
        if (!path) throw std::runtime_error("unknown font id " + std::to_string(id));
        return fontCache_[id] = fnt::parse(readOrThrow(vfs_, *path));
    }

    itm::Item item(uint16_t id) {
        const std::string* path = items_.find(id);
        if (!path) throw std::runtime_error("unknown item id " + std::to_string(id));
        return itm::parse(readOrThrow(vfs_, *path));
    }

    std::string itemImagePath(uint16_t id) {
        const std::string* path = itemImages_.find(id);
        if (!path) throw std::runtime_error("unknown item image id " + std::to_string(id));
        return *path;
    }

    std::string cursorPath(uint16_t id) {
        const std::string* path = cursors_.find(id);
        if (!path) throw std::runtime_error("unknown cursor id " + std::to_string(id));
        return *path;
    }

private:
    SDL_Renderer* ren_;
    Vfs vfs_;
    mgr::Index fonts_, items_, itemImages_, cursors_;
    std::map<uint16_t, fnt::Font> fontCache_;
    std::map<std::string, gim::Image> images_;
    std::map<std::string, Texture> textures_;
};

// One control of a loaded interface, reduced to what the front end needs.
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

    bool interactive() const {
        return control->classId == itf::Button || control->classId == itf::AnimatedBtn;
    }
};

class Screen {
public:
    Screen(Assets& assets, const std::string& itfPath) {
        itf_ = itf::parse(readOrThrow(assets.vfs(), itfPath));
        // The engine copies the background's palette and colours all text with it.
        for (const auto& c : itf_.controls)
            if (c.classId == itf::Backgd && !c.images.empty()) palette_ = assets.image(c.images[0]).palette;

        for (const auto& c : itf_.controls) {
            Widget w;
            w.control = &c;
            w.rect = {float(c.x), float(c.y), 0, 0};
            for (const auto& path : c.images) w.images.push_back(&assets.texture(path));
            if (!w.images.empty()) w.rect.w = float(w.images[0]->w), w.rect.h = float(w.images[0]->h);
            if (c.classId == itf::Button && !c.values.empty()) w.action = c.values[0];
            if (c.textBox && !c.text.empty()) {
                const auto img = ui::renderTextBox(assets.font(c.textBox->font), palette_, *c.textBox, c.text);
                w.text = assets.upload(img.pixels, img.width, img.height);
                w.rect.w = float(img.width), w.rect.h = float(img.height);
            }
            if (c.classId == itf::AnimatedBtn && c.values.size() >= 2) {
                const itm::Item item = assets.item(uint16_t(c.values[0]));
                if (!item.animation || item.frameCount == 0) throw std::runtime_error("item is not animated: " + item.name);
                w.strip = &assets.texture(assets.itemImagePath(item.image));
                w.frameH = w.strip->h / item.frameCount;
                w.anim = ui::ItemAnimation(*item.animation, item.frameCount);
                w.rect.w = float(w.strip->w), w.rect.h = float(w.frameH);
                w.action = c.values[1];
            }
            widgets_.push_back(std::move(w));
        }
        // Higher z first; the buttons (z = -1000) end up on top.
        order_.resize(widgets_.size());
        for (size_t i = 0; i < order_.size(); ++i) order_[i] = i;
        std::stable_sort(order_.begin(), order_.end(),
                         [&](size_t a, size_t b) { return widgets_[a].control->z > widgets_[b].control->z; });
    }
    ~Screen() {
        for (auto& w : widgets_) SDL_DestroyTexture(w.text.tex);
    }

    // Hover tracking; on `click`, presses the topmost widget under the point and
    // returns its action (else -1).
    long pointer(float x, float y, bool click) {
        focus_.reset();
        long action = -1;
        bool taken = false;
        for (size_t i = order_.size(); i-- > 0;) {  // topmost first
            Widget& w = widgets_[order_[i]];
            if (!w.interactive()) continue;
            const bool inside = x >= w.rect.x && x < w.rect.x + w.rect.w && y >= w.rect.y && y < w.rect.y + w.rect.h;
            const bool hit = inside && !taken;
            taken = taken || inside;
            setHover(w, hit);
            if (hit && click) action = activate(w);
        }
        return action;
    }

    // Keyboard / gamepad: move the focus through the interactive widgets.
    void moveFocus(int delta) {
        std::vector<size_t> ids;
        for (size_t i = 0; i < widgets_.size(); ++i)
            if (widgets_[i].interactive()) ids.push_back(i);
        if (ids.empty()) return;
        const int n = int(ids.size());
        int pos = 0;
        if (focus_) {
            pos = int(std::find(ids.begin(), ids.end(), *focus_) - ids.begin());
            pos = ((pos + delta) % n + n) % n;
        }
        focus_ = ids[size_t(pos)];
        for (size_t i : ids) setHover(widgets_[i], i == *focus_);
    }
    long activateFocus() { return focus_ ? activate(widgets_[*focus_]) : -1; }

    void select(uint32_t action) {
        for (auto& w : widgets_)
            if (w.control->classId == itf::AnimatedBtn) {
                w.selected = w.action == action;
                w.anim.setState(w.selected ? ui::ItemAnimation::State::Pressed
                                           : (w.hovered ? ui::ItemAnimation::State::Hover : ui::ItemAnimation::State::Idle));
            }
    }

    void update(uint64_t nowMs) {
        for (auto& w : widgets_)
            if (w.strip) w.anim.update(nowMs);
    }

    void draw(SDL_Renderer* ren) const {
        for (size_t i : order_) {
            const Widget& w = widgets_[i];
            if (w.text.tex) {
                SDL_RenderTexture(ren, w.text.tex, nullptr, &w.rect);
            } else if (w.strip) {
                // Options, Load and Exit press on to one past their last frame; how the
                // engine draws that is unknown, so hold the last frame.
                const int frame = std::min<int>(w.anim.frame(), w.strip->h / w.frameH - 1);
                const SDL_FRect src{0, float(frame * w.frameH), float(w.strip->w), float(w.frameH)};
                SDL_RenderTexture(ren, w.strip->tex, &src, &w.rect);
            } else if (!w.images.empty()) {
                // Buttons: off / on (hover) / pressed images.
                size_t img = w.pressed && w.images.size() > 2 ? 2 : w.hovered && w.images.size() > 1 ? 1 : 0;
                const Texture* t = w.images[img];
                const SDL_FRect r{w.rect.x, w.rect.y, float(t->w), float(t->h)};
                SDL_RenderTexture(ren, t->tex, nullptr, &r);
            }
        }
    }

private:
    void setHover(Widget& w, bool on) {
        if (w.hovered == on) return;
        w.hovered = on;
        if (!on) w.pressed = false;
        if (w.strip && !w.selected)
            w.anim.setState(on ? ui::ItemAnimation::State::Hover : ui::ItemAnimation::State::Idle);
    }
    // The engine sends the control's action as soon as it is pressed (GCtrlAnimatedBtn_v7).
    long activate(Widget& w) {
        w.pressed = true;
        if (w.strip) w.anim.setState(ui::ItemAnimation::State::Pressed);
        return long(w.action);
    }

    itf::Interface itf_;
    std::vector<gim::Color> palette_;
    std::vector<Widget> widgets_;
    std::vector<size_t> order_;
    std::optional<size_t> focus_;
};

// The animated mouse cursor (cursor.mgr id 1, Main.acr).
class Cursor {
public:
    Cursor(Assets& assets) {
        const auto acr = acr::parse(readOrThrow(assets.vfs(), assets.cursorPath(1)));
        hotX_ = acr.hotX, hotY_ = acr.hotY;
        // frameDelay is 10 in every file; read here as hundredths of a second (a guess).
        delayMs_ = acr.frameDelay ? acr.frameDelay * 10 : 100;
        for (const auto& f : acr.frames) frames_.push_back(&assets.texture(f));
    }
    void draw(SDL_Renderer* ren, float x, float y, uint64_t nowMs) const {
        if (frames_.empty()) return;
        const Texture* t = frames_[size_t(nowMs / delayMs_) % frames_.size()];
        const SDL_FRect r{x - hotX_, y - hotY_, float(t->w), float(t->h)};
        SDL_RenderTexture(ren, t->tex, nullptr, &r);
    }

private:
    std::vector<const Texture*> frames_;
    float hotX_ = 0, hotY_ = 0;
    uint64_t delayMs_ = 100;
};

struct Options {
    std::string data, screen = "startup", screenshot;
    uint64_t timeMs = 0;
    std::optional<SDL_FPoint> mouse;
};

class FrontEnd {
public:
    FrontEnd(SDL_Renderer* ren, const Options& opt) : ren_(ren), assets_(ren, opt.data), cursor_(assets_) {
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
        screen_ = std::make_unique<Screen>(assets_, itf);
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
    std::unique_ptr<Screen> screen_;
    std::string current_;
    std::optional<std::string> pending_;
    SDL_FPoint mouse_{kScreenW / 2.0f, kScreenH / 2.0f};
    bool showCursor_ = false, running_ = true, stickHeld_ = false;
    uint64_t now_ = 0;
};

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string a = argv[i], v = argv[i + 1];
        if (a == "--data") o.data = v;
        else if (a == "--screen") o.screen = v;
        else if (a == "--screenshot") o.screenshot = v;
        else if (a == "--time") o.timeMs = std::stoull(v);
        else if (a == "--mouse") {
            const auto comma = v.find(',');
            if (comma == std::string::npos) throw std::runtime_error("--mouse expects x,y");
            o.mouse = SDL_FPoint{std::stof(v.substr(0, comma)), std::stof(v.substr(comma + 1))};
        }
    }
    if (o.data.empty()) throw std::runtime_error("usage: harbinger --data <Harbinger install dir> [--screen startup|new] "
                                                 "[--screenshot out.bmp [--time ms] [--mouse x,y]]");
    return o;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options opt = parseArgs(argc, argv);
        const bool shot = !opt.screenshot.empty();
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
                while (game.running()) {
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
