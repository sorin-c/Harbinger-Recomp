#include "app/screen.hpp"

#include <algorithm>
#include <stdexcept>

#include "formats/acr.hpp"
#include "ui/text.hpp"

namespace harbinger::app {

bool Widget::interactive() const {
    return control->classId == itf::Button || control->classId == itf::AnimatedBtn;
}

Screen::Screen(Assets& assets, const std::string& itfPath, SoundFn sound) : sound_(std::move(sound)) {
    itf_ = itf::parse(assets.read(itfPath));
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

Screen::~Screen() {
    for (auto& w : widgets_) SDL_DestroyTexture(w.text.tex);
}

long Screen::pointer(float x, float y, bool click) {
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

void Screen::moveFocus(int delta) {
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

long Screen::activateFocus() { return focus_ ? activate(widgets_[*focus_]) : -1; }

void Screen::select(uint32_t action) {
    for (auto& w : widgets_)
        if (w.control->classId == itf::AnimatedBtn) {
            w.selected = w.action == action;
            w.anim.setState(w.selected ? ui::ItemAnimation::State::Pressed
                                       : (w.hovered ? ui::ItemAnimation::State::Hover : ui::ItemAnimation::State::Idle));
        }
}

void Screen::update(uint64_t nowMs) {
    for (auto& w : widgets_)
        if (w.strip) w.anim.update(nowMs);
}

void Screen::draw(SDL_Renderer* ren) const {
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

void Screen::setHover(Widget& w, bool on) {
    if (w.hovered == on) return;
    w.hovered = on;
    if (!on) w.pressed = false;
    if (w.strip && !w.selected) {
        w.anim.setState(on ? ui::ItemAnimation::State::Hover : ui::ItemAnimation::State::Idle);
        if (on) playSlot(kHoverSound);
    }
}

// The engine sends the control's action as soon as it is pressed (GCtrlAnimatedBtn_v7).
long Screen::activate(Widget& w) {
    w.pressed = true;
    if (w.strip) {
        w.anim.setState(ui::ItemAnimation::State::Pressed);
        playSlot(kPressSound);
    }
    return long(w.action);
}

void Screen::playSlot(int slot) {
    if (sound_ && size_t(slot) < itf_.sounds.size() && itf_.sounds[size_t(slot)] != 0)
        sound_(slot, itf_.sounds[size_t(slot)]);
}

Cursor::Cursor(Assets& assets) {
    const auto acr = acr::parse(assets.read(assets.cursorPath(1)));
    hotX_ = acr.hotX, hotY_ = acr.hotY;
    // frameDelay is 10 in every file; read here as hundredths of a second (a guess).
    delayMs_ = acr.frameDelay ? acr.frameDelay * 10 : 100;
    for (const auto& f : acr.frames) frames_.push_back(&assets.texture(f));
}

void Cursor::draw(SDL_Renderer* ren, float x, float y, uint64_t nowMs) const {
    if (frames_.empty()) return;
    const Texture* t = frames_[size_t(nowMs / delayMs_) % frames_.size()];
    const SDL_FRect r{x - hotX_, y - hotY_, float(t->w), float(t->h)};
    SDL_RenderTexture(ren, t->tex, nullptr, &r);
}

}  // namespace harbinger::app
