#include "app/screen.hpp"

#include <algorithm>
#include <stdexcept>

#include "formats/acr.hpp"

namespace harbinger::app {

// Class id of the rebuild's choice control; the factory has no class 0xFFFF.
constexpr uint16_t kChoice = 0xFFFF;

bool Widget::interactive() const {
    switch (cls()) {
        case kChoice:
        case itf::Button:
        case itf::ScrollUp:
        case itf::ScrollDown:
        case itf::TextBtn:
        case itf::CheckBox:
        case itf::SliderBar:
        case itf::AnimatedBtn: return true;
        default: return false;
    }
}

Screen::Screen(Assets& assets, const std::string& itfPath, SoundFn sound) : assets_(assets), sound_(std::move(sound)) {
    itf_ = itf::parse(assets.read(itfPath));
    // The engine copies the background's palette and colours all text with it.
    for (const auto& c : itf_.controls)
        if (c.classId == itf::Backgd && !c.images.empty()) palette_ = assets.image(c.images[0]).palette;

    for (const auto& c : itf_.controls) {
        Widget w;
        w.control = &c;
        w.rect = {float(c.x), float(c.y), 0, 0};
        w.text = c.text;
        for (const auto& path : c.images) w.images.push_back(&assets.texture(path, &palette_));
        if (!w.images.empty()) w.rect.w = float(w.images[0]->w), w.rect.h = float(w.images[0]->h);
        switch (c.classId) {
            case itf::Button:
            case itf::ScrollUp:
            case itf::ScrollDown:
            case itf::CheckBox:
            case itf::SliderBar:
                if (!c.values.empty()) w.action = c.values[0];
                break;
            case itf::TextBtn:
                if (c.values.size() >= 2) w.action = c.values[1];
                break;
            case itf::AnimatedBtn:
                if (c.values.size() >= 2) w.action = c.values[1];
                break;
            default: break;
        }
        if (c.classId == itf::SliderBar && w.images.size() >= 6) {
            // GCtrlSliderBar_v1: the track is image 0's width, the arrows image 3's.
            w.slider.width = w.images[0]->w;
            w.slider.arrow = w.images[3]->w;
        }
        if ((c.classId == itf::AnimatedBtn || c.classId == itf::Icon) && !c.values.empty()) {
            const itm::Item item = assets.item(uint16_t(c.values[0]));
            if (!item.animation || item.frameCount == 0) throw std::runtime_error("item is not animated: " + item.name);
            w.strip = &assets.itemTexture(assets.itemImagePath(item.image));
            w.frameH = w.strip->h / item.frameCount;
            w.anim = ui::ItemAnimation(*item.animation, item.frameCount);
            w.rect.w = float(w.strip->w), w.rect.h = float(w.frameH);
            // An Icon is centred on its position (the Load screen's thumbnail
            // frame lines up with a capture of the original this way).
            if (c.classId == itf::Icon) w.rect.x -= w.rect.w / 2, w.rect.y -= w.rect.h / 2;
        }
        widgets_.push_back(std::move(w));
    }
    for (auto& w : widgets_) renderFaces(w);
    // The background first, then higher z first; the buttons (z = -1000) end
    // up on top. The Load screen's scroll arrows (z 5 and 10) show over its
    // background in the original, so the background cannot simply sort by z.
    order_.resize(widgets_.size());
    for (size_t i = 0; i < order_.size(); ++i) order_[i] = i;
    std::stable_sort(order_.begin(), order_.end(), [&](size_t a, size_t b) {
        const Widget &wa = widgets_[a], &wb = widgets_[b];
        if ((wa.cls() == itf::Backgd) != (wb.cls() == itf::Backgd)) return wa.cls() == itf::Backgd;
        return wa.control->z > wb.control->z;
    });
}

Screen::~Screen() {
    for (auto& w : widgets_)
        for (auto& f : w.face) SDL_DestroyTexture(f.tex);
}

void Screen::renderFaces(Widget& w) {
    const itf::Control& c = *w.control;
    for (auto& f : w.face) SDL_DestroyTexture(f.tex), f = {};
    if (c.classId == itf::TextBtn && w.images.size() >= 2 && !c.values.empty()) {
        // The u32 holds two font.mgr ids: low half normally, high half pressed.
        const uint16_t fonts[2] = {uint16_t(c.values[0] & 0xFFFF), uint16_t(c.values[0] >> 16)};
        for (int i = 0; i < 2; ++i) {
            const auto img = ui::renderTextButton(assets_.rgba(c.images[size_t(i)], &palette_), assets_.font(fonts[i]), palette_, w.text);
            w.face[i] = assets_.upload(img.pixels, img.width, img.height);
        }
    } else if (c.textBox) {
        const itf::TextBox& b = *c.textBox;
        const ui::BoxFonts fonts{&assets_.font(b.font), &assets_.font(b.fontAlt), &assets_.font(b.fontStar)};
        std::optional<render::RgbaImage> bg;
        if (!c.images.empty()) bg = assets_.rgba(c.images[0], &palette_);
        const auto img = ui::renderTextBox(fonts, palette_, b, w.text, -1, bg ? &*bg : nullptr);
        w.face[0] = assets_.upload(img.pixels, img.width, img.height);
        w.rect.w = float(img.width), w.rect.h = float(img.height);
    }
}

Widget* Screen::at(float x, float y) {
    for (size_t i = order_.size(); i-- > 0;) {  // topmost first
        Widget& w = widgets_[order_[i]];
        if (w.interactive() && x >= w.rect.x && x < w.rect.x + w.rect.w && y >= w.rect.y && y < w.rect.y + w.rect.h)
            return &w;
    }
    return nullptr;
}

void Screen::pointerMove(float x, float y) {
    focus_.reset();
    pointerX_ = x;
    const Widget* hit = at(x, y);
    for (auto& w : widgets_)
        if (w.interactive()) setHover(w, &w == hit);
}

void Screen::pointerDown(float x, float y) {
    pointerMove(x, y);
    Widget* w = at(x, y);
    if (!w) return;
    captured_ = size_t(w - widgets_.data());
    event(*w, ui::PointerEvent::Down);
}

void Screen::pointerUp(float x, float y) {
    pointerMove(x, y);
    if (!captured_) return;
    Widget& w = widgets_[*captured_];
    captured_.reset();
    event(w, at(x, y) == &w ? ui::PointerEvent::Up : ui::PointerEvent::Cancel);
}

void Screen::event(Widget& w, ui::PointerEvent e) {
    const size_t index = size_t(&w - widgets_.data());
    bool notify = false;
    switch (w.cls()) {
        case itf::AnimatedBtn:
            // GCtrlAnimatedBtn_v7: the click plays the press section and sends the action.
            if (e == ui::PointerEvent::Up) {
                w.anim.setState(ui::ItemAnimation::State::Pressed);
                playSlot(kPressSound);
                notify = true;
            }
            break;
        case itf::CheckBox: notify = w.check.event(e); break;
        case itf::SliderBar: notify = w.slider.event(e, int(pointerX_ - w.rect.x), now_); break;
        case itf::Button:
        case itf::ScrollUp:
        case itf::ScrollDown:
        case itf::TextBtn: notify = w.button.event(e); break;
        case kChoice:
            if (e == ui::PointerEvent::Up) {
                w.step = pointerX_ < w.rect.x + w.rect.w / 2 ? -1 : 1;
                notify = true;
            }
            break;
        default: break;
    }
    if (notify) notices_.push_back({index, long(int32_t(w.action))});
}

void Screen::moveFocus(int delta) {
    // Reading order (top to bottom, then left to right); a rebuild addition,
    // the original's menus are mouse only.
    std::vector<size_t> ids;
    for (size_t i = 0; i < widgets_.size(); ++i)
        if (widgets_[i].interactive() && widgets_[i].button.state != ui::ButtonState::Disabled) ids.push_back(i);
    if (ids.empty()) return;
    std::stable_sort(ids.begin(), ids.end(), [&](size_t a, size_t b) {
        const SDL_FRect &ra = widgets_[a].rect, &rb = widgets_[b].rect;
        return ra.y != rb.y ? ra.y < rb.y : ra.x < rb.x;
    });
    const int n = int(ids.size());
    int pos = 0;
    if (focus_) {
        pos = int(std::find(ids.begin(), ids.end(), *focus_) - ids.begin());
        pos = ((pos + delta) % n + n) % n;
    }
    focus_ = ids[size_t(pos)];
    for (size_t i : ids) setHover(widgets_[i], i == *focus_);
}

void Screen::activateFocus() {
    if (!focus_) return;
    Widget& w = widgets_[*focus_];
    if (w.cls() == itf::SliderBar) return;
    if (w.cls() == kChoice) return adjustFocus(1);
    event(w, ui::PointerEvent::Down);
    event(w, ui::PointerEvent::Up);
}

void Screen::adjustFocus(int dir) {
    if (focus_ && widgets_[*focus_].cls() == kChoice) {
        widgets_[*focus_].step = dir;
        notices_.push_back({*focus_, long(int32_t(widgets_[*focus_].action))});
    } else if (focus_ && widgets_[*focus_].cls() == itf::SliderBar) {
        Widget& w = widgets_[*focus_];
        w.slider.nudge(dir);
        notices_.push_back({*focus_, long(int32_t(w.action))});
    } else {
        moveFocus(dir);
    }
}

std::vector<Notice> Screen::takeNotices() { return std::exchange(notices_, {}); }

void Screen::select(uint32_t action) {
    for (auto& w : widgets_)
        if (w.cls() == itf::AnimatedBtn) {
            w.selected = w.action == action;
            w.anim.setState(w.selected ? ui::ItemAnimation::State::Pressed
                                       : (w.hovered ? ui::ItemAnimation::State::Hover : ui::ItemAnimation::State::Idle));
        }
}

void Screen::setSlider(size_t control, uint32_t action, double value) {
    Widget& w = widgets_.at(control);
    w.action = action;
    w.slider.set(value);
}

double Screen::sliderChanged(size_t control) { return widgets_.at(control).slider.changed(); }

void Screen::setCheck(size_t control, uint32_t action, bool on) {
    Widget& w = widgets_.at(control);
    w.action = action;
    w.check.set(on);
}

bool Screen::checked(size_t control) const { return widgets_.at(control).check.checked; }

void Screen::setText(size_t control, const std::string& text) {
    Widget& w = widgets_.at(control);
    w.text = text;
    renderFaces(w);
}

size_t Screen::addChoice(int x, int y, int width, uint16_t font, uint32_t action) {
    itf::Control& c = extras_.emplace_back();
    c.classId = kChoice;
    c.x = int16_t(x), c.y = int16_t(y), c.z = -1000;
    c.name = "Choice";
    itf::TextBox box;
    box.width = uint16_t(width);
    box.height = 18;
    box.font = box.fontAlt = box.fontStar = font;
    box.align = 0x20;
    c.textBox = box;
    Widget w;
    w.control = &c;
    w.rect = {float(x), float(y), float(width), 18};
    w.action = action;
    widgets_.push_back(std::move(w));
    order_.push_back(widgets_.size() - 1);
    return widgets_.size() - 1;
}

void Screen::setDisabled(size_t control, bool disabled) {
    ui::Button& b = widgets_.at(control).button;
    b.state = b.saved = disabled ? ui::ButtonState::Disabled : ui::ButtonState::Normal;
}

void Screen::update(uint64_t nowMs) {
    now_ = nowMs;
    if (captured_) event(widgets_[*captured_], ui::PointerEvent::Held);
    for (auto& w : widgets_)
        if (w.strip) w.anim.update(nowMs);
}

void Screen::draw(SDL_Renderer* ren) const {
    auto blit = [&](const Texture* t, float x, float y, const SDL_FRect* src = nullptr) {
        if (!t || !t->tex) return;
        const SDL_FRect r{x, y, src ? src->w : float(t->w), src ? src->h : float(t->h)};
        SDL_RenderTexture(ren, t->tex, src, &r);
    };
    auto image = [&](const Widget& w, size_t i) { return i < w.images.size() ? w.images[i] : nullptr; };

    for (size_t i : order_) {
        const Widget& w = widgets_[i];
        const float x = w.rect.x, y = w.rect.y;
        switch (w.cls()) {
            case itf::AnimatedBtn:
            case itf::Icon: {
                // Options, Load and Exit press on to one past their last frame; how the
                // engine draws that is unknown, so hold the last frame.
                const int frame = std::min<int>(w.anim.frame(), w.strip->h / w.frameH - 1);
                const SDL_FRect src{0, float(frame * w.frameH), float(w.strip->w), float(w.frameH)};
                blit(w.strip, x, y, &src);
                break;
            }
            case itf::TextBtn:
                // GCtrlTextBtn_v3 draws nothing while disabled.
                if (w.button.state != ui::ButtonState::Disabled)
                    blit(&w.face[w.button.state == ui::ButtonState::Pressed ? 1 : 0], x, y);
                break;
            case itf::CheckBox: blit(image(w, size_t(w.check.image())), x, y); break;
            case itf::SliderBar: {
                // GCtrlSliderBar_v3: dark track, the lit track up to the value,
                // then each arrow lit while it is held.
                // Drawn on its own black surface, which shows through the
                // track's keyed pixels (black in a capture of the original).
                const ui::SliderBar& s = w.slider;
                SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
                SDL_RenderFillRect(ren, &w.rect);
                blit(image(w, 0), x, y);
                if (s.pos > 0 && image(w, 1)) {
                    const SDL_FRect src{float(s.arrow), 0, float(s.pos), float(image(w, 1)->h)};
                    blit(image(w, 1), x + float(s.arrow), y, &src);
                }
                blit(image(w, s.leftHeld ? 3 : 2), x, y);
                blit(image(w, s.rightHeld ? 5 : 4), x + float(s.width - s.arrow), y);
                break;
            }
            case itf::Button:
            case itf::ScrollUp:
            case itf::ScrollDown: {
                // GCtrlButton_v14: image 1 normally, 2 pressed, 0 disabled.
                const ui::ButtonState st = w.button.state;
                blit(image(w, st == ui::ButtonState::Pressed ? 2 : st == ui::ButtonState::Disabled ? 0 : 1), x, y);
                break;
            }
            default:
                if (w.face[0].tex) blit(&w.face[0], x, y);
                else blit(image(w, 0), x, y);
                break;
        }
    }
    // Keyboard / gamepad focus (a rebuild addition). Animated buttons show it
    // as their hover animation instead.
    if (focus_ && widgets_[*focus_].cls() != itf::AnimatedBtn) {
        const SDL_FRect& r = widgets_[*focus_].rect;
        const SDL_FRect box{r.x - 2, r.y - 2, r.w + 4, r.h + 4};
        SDL_SetRenderDrawColor(ren, 96, 255, 160, 255);
        SDL_RenderRect(ren, &box);
    }
}

void Screen::setHover(Widget& w, bool on) {
    if (w.hovered == on) return;
    w.hovered = on;
    if (w.strip && !w.selected) {
        w.anim.setState(on ? ui::ItemAnimation::State::Hover : ui::ItemAnimation::State::Idle);
        if (on) playSlot(kHoverSound);
    }
}

void Screen::playSlot(int slot, bool restart) {
    if (sound_ && size_t(slot) < itf_.sounds.size() && itf_.sounds[size_t(slot)] != 0)
        sound_(slot, itf_.sounds[size_t(slot)], restart);
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
