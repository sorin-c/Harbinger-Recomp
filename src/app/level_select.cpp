#include "app/level_select.hpp"

#include <algorithm>
#include <cmath>

#include "app/hud.hpp"
#include "ui/text.hpp"

namespace harbinger::app {

namespace {

const char* const kLevelItf = "res\\common\\interface\\level\\level.itf";

}  // namespace

LevelSelect::LevelSelect(Assets& assets, int screenW, int screenH) : assets_(assets) {
    itf_ = itf::parse(assets.read(kLevelItf));
    const itf::Control* bg = nullptr;
    for (const auto& c : itf_.controls)
        if (c.classId == itf::Backgd && !c.images.empty()) bg = &c;
    // The interface's text and art take the background's palette, as in
    // the other panels; the background is placed by the panel kind (0x0b:
    // centred, FUN_004c4af0) and the other controls are offsets from it,
    // as in the HUD.
    SDL_FPoint origin{0, 0};
    if (bg) {
        palette_ = assets.image(bg->images[0]).palette;
        origin = panelPosition(itf_.kind, bg->x, bg->y, screenW, screenH);
    }
    parts_.reserve(itf_.controls.size());
    for (const auto& c : itf_.controls) {
        Part p;
        p.control = &c;
        for (const auto& img : c.images) p.images.push_back(&assets.texture(img, &palette_));
        p.rect = c.classId == itf::Backgd ? SDL_FRect{origin.x, origin.y, 0, 0}
                                          : SDL_FRect{origin.x + float(c.x), origin.y + float(c.y), 0, 0};
        if (!p.images.empty()) p.rect.w = float(p.images[0]->w), p.rect.h = float(p.images[0]->h);
        if (c.textBox) p.rect.w = c.textBox->width, p.rect.h = c.textBox->height;
        parts_.push_back(p);
    }
    std::stable_sort(parts_.begin(), parts_.end(), [](const Part& a, const Part& b) {
        const bool ba = a.control->classId == itf::Backgd, bb = b.control->classId == itf::Backgd;
        if (ba != bb) return ba;
        return a.control->z > b.control->z;
    });
    for (auto& p : parts_) {
        if (p.control->classId == itf::Button) cancel_ = &p;
        if (p.control->classId == itf::ScrollUp) up_ = &p;
        if (p.control->classId == itf::ScrollDown) down_ = &p;
        if (p.control->textBox) box_ = &p;
    }
}

LevelSelect::~LevelSelect() { SDL_DestroyTexture(face_.tex); }

void LevelSelect::open(const std::string& text) {
    lines_.clear();
    size_t at = 0;
    while (at < text.size()) {
        const size_t end = text.find('\n', at);
        lines_.push_back(text.substr(at, end == std::string::npos ? std::string::npos : end - at));
        if (end == std::string::npos) break;
        at = end + 1;
    }
    top_ = 0;
    highlight_ = -1;
    choice_.reset();
    pressed_ = nullptr;
    open_ = true;
    render();
}

void LevelSelect::close() { open_ = false; }

std::optional<int> LevelSelect::takeChoice() {
    auto c = choice_;
    choice_.reset();
    return c;
}

bool LevelSelect::inside(const Part& p, float x, float y) const {
    return x >= p.rect.x && x < p.rect.x + p.rect.w && y >= p.rect.y && y < p.rect.y + p.rect.h;
}

int LevelSelect::visibleLines() const {
    if (!box_) return 0;
    const int h = assets_.font(box_->control->textBox->font).lineHeight;
    return h > 0 ? box_->control->textBox->height / h : 0;
}

int LevelSelect::lineAt(float x, float y) const {
    // GCtrlStaticTextBox_v7: the row under the pointer, by the box font's
    // line height.
    if (!box_ || !inside(*box_, x, y)) return -1;
    const itf::TextBox& b = *box_->control->textBox;
    const int h = assets_.font(b.font).lineHeight;
    if (h <= 0) return -1;
    const int row = int(std::floor((y - box_->rect.y - float(b.padY)) / float(h)));
    const int line = top_ + row;
    return row >= 0 && line < int(lines_.size()) ? line : -1;
}

void LevelSelect::scroll(int delta) {
    const int most = std::max(0, int(lines_.size()) - visibleLines());
    const int t = std::clamp(top_ + delta, 0, most);
    if (t != top_) top_ = t, render();
}

void LevelSelect::render() {
    SDL_DestroyTexture(face_.tex);
    face_ = {};
    if (!box_) return;
    const itf::TextBox& b = *box_->control->textBox;
    std::string text;
    for (size_t i = size_t(top_); i < lines_.size(); ++i) text += lines_[i] + '\n';
    const ui::BoxFonts fonts{&assets_.font(b.font), &assets_.font(b.fontAlt), &assets_.font(b.fontStar)};
    const auto img = ui::renderTextBox(fonts, palette_, b, text, highlight_ >= 0 ? highlight_ - top_ : -1);
    face_ = assets_.upload(img.pixels, img.width, img.height);
}

void LevelSelect::pointerMove(float x, float y) {
    if (!open_) return;
    const int line = lineAt(x, y);
    if (line != highlight_) highlight_ = line, render();
}

void LevelSelect::pointerDown(float x, float y) {
    if (!open_) return;
    for (Part* p : {cancel_, up_, down_})
        if (p && inside(*p, x, y)) {
            p->button.event(ui::PointerEvent::Down);
            pressed_ = p;
            return;
        }
}

void LevelSelect::pointerUp(float x, float y) {
    if (!open_) return;
    if (pressed_) {
        Part* p = pressed_;
        pressed_ = nullptr;
        if (!p->button.event(inside(*p, x, y) ? ui::PointerEvent::Up : ui::PointerEvent::Cancel)) return;
        if (p == cancel_) cancel();
        else scroll(p == up_ ? -1 : 1);
        return;
    }
    // GCtrlStaticTextBox_v7, case 3: releasing over a line sends its index
    // (message 0x17, FUN_004a1a80) and then the box's id (message 5), which
    // starts the trip at once (FUN_004a1d20).
    if (const int line = lineAt(x, y); line >= 0) choice_ = line;
}

void LevelSelect::moveFocus(int delta) {
    if (!open_ || lines_.empty()) return;
    highlight_ = highlight_ < 0 ? (delta > 0 ? top_ : top_ + std::max(0, visibleLines() - 1))
                                : std::clamp(highlight_ + delta, 0, int(lines_.size()) - 1);
    highlight_ = std::min(highlight_, int(lines_.size()) - 1);
    if (highlight_ < top_) top_ = highlight_;
    if (highlight_ >= top_ + visibleLines()) top_ = highlight_ - visibleLines() + 1;
    render();
}

void LevelSelect::pick() {
    if (open_ && highlight_ >= 0 && highlight_ < int(lines_.size())) choice_ = highlight_;
}

void LevelSelect::cancel() {
    if (open_) choice_ = -1;
}

void LevelSelect::draw(SDL_Renderer* ren) const {
    if (!open_) return;
    for (const auto& p : parts_) {
        auto blit = [&](const Texture* t) {
            if (!t || !t->tex) return;
            const SDL_FRect dst{p.rect.x, p.rect.y, float(t->w), float(t->h)};
            SDL_RenderTexture(ren, t->tex, nullptr, &dst);
        };
        auto image = [&](size_t i) { return i < p.images.size() ? p.images[i] : nullptr; };
        switch (p.control->classId) {
            case itf::Button:
            case itf::ScrollUp:
            case itf::ScrollDown: {
                // GCtrlButton_v14: image 1 normally, 2 pressed, 0 disabled.
                // An arrow with nothing to scroll shows disabled (rebuild).
                bool disabled = false;
                if (&p == up_) disabled = top_ == 0;
                if (&p == down_) disabled = top_ + visibleLines() >= int(lines_.size());
                const ui::ButtonState st = p.button.state;
                blit(image(disabled ? 0 : st == ui::ButtonState::Pressed ? 2 : 1));
                break;
            }
            default:
                if (p.control->textBox) blit(&face_);
                else blit(image(0));
                break;
        }
    }
}

}  // namespace harbinger::app
