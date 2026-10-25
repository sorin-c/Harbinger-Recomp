#include "app/dialogue.hpp"

#include <algorithm>
#include <cmath>

#include "ui/text.hpp"

namespace harbinger::app {

namespace {

constexpr const char* kItf = "res/common/interface/dialog/npcdialog.itf";
constexpr uint16_t kPictureSlot = 0x39;  // GCtrlPictureSlot (the portrait)

}  // namespace

Dialogue::Dialogue(Assets& assets, int screenW, int screenH, Screen::SoundFn sound)
    : assets_(assets), sound_(std::move(sound)) {
    itf_ = itf::parse(assets.read(kItf));
    for (const auto& c : itf_.controls) {
        switch (c.classId) {
            case itf::Backgd: background_ = &c; break;
            case itf::StaticTextBox: text_ = &c; break;
            case kPictureSlot: portrait_ = &c; break;
            case itf::ScrollUp: upCtl_ = &c; break;
            case itf::ScrollDown: downCtl_ = &c; break;
            case itf::TextBtn: responseCtl_.push_back(&c); break;
            default: break;  // the Close button has no picture: Escape and B stand for it
        }
    }
    if (!background_ || !text_ || !text_->textBox || responseCtl_.empty())
        throw std::runtime_error("npcdialog.itf: unexpected layout");
    palette_ = assets.image(background_->images.at(0)).palette;
    backgroundTex_ = &assets.texture(background_->images[0], &palette_);
    for (int i = 0; i < 3; ++i) {
        if (upCtl_ && size_t(i) < upCtl_->images.size()) up_[i] = &assets.texture(upCtl_->images[size_t(i)], &palette_);
        if (downCtl_ && size_t(i) < downCtl_->images.size())
            down_[i] = &assets.texture(downCtl_->images[size_t(i)], &palette_);
    }
    // Interface kind 6 sits at the background's position at 800x600; at
    // 1024x768 the engine moves it by (+0x70, +0x5e) (docs/dialogue.md).
    // Other sizes (the rebuild's) scale that move.
    x_ = float(background_->x) + float(screenW - 800) / 2.0f;
    y_ = float(background_->y) + float(screenH - 600) * 94.0f / 168.0f;
    // The other controls are placed from the background's corner.
    if (up_[1]) upBtn_.rect = {x_ + upCtl_->x, y_ + upCtl_->y, float(up_[1]->w), float(up_[1]->h)};
    if (down_[1]) downBtn_.rect = {x_ + downCtl_->x, y_ + downCtl_->y, float(down_[1]->w), float(down_[1]->h)};
}

Dialogue::~Dialogue() {
    SDL_DestroyTexture(textTex_.tex);
    for (auto& r : responses_)
        for (auto& f : r.face) SDL_DestroyTexture(f.tex);
}

void Dialogue::sync(const world::Story& story) {
    if (story.revision() == revision_) return;
    revision_ = story.revision();
    layout(story);
}

void Dialogue::layout(const world::Story& story) {
    for (auto& r : responses_)
        for (auto& f : r.face) SDL_DestroyTexture(f.tex);
    responses_.clear();
    choice_.reset();
    captured_ = -1;
    const dlg::Node* node = story.node();
    if (!node) return;

    // FUN_004f7f40 -> FUN_0049f710: message 0x26 with "" empties the
    // responses; then each response's text goes to the first empty
    // button, and the buttons already filled move up by its height, so
    // the last response sits at the .itf position and the others stack
    // above it. Message 0x15 then shortens the text box by as many lines.
    const auto& offered = story.responses();
    const size_t n = std::min(offered.size(), responseCtl_.size());
    for (size_t i = 0; i < n; ++i) {
        const itf::Control& c = *responseCtl_[i];
        std::string caption = offered[i]->text;
        if (!caption.empty() && caption[0] == '*') caption.erase(0, 1);  // greyed: none in the shipped files
        Response r;
        // GCtrlTextBtn_v7 with 0x26: a filled button moves by its size
        // times (0, -1) (FUN_004c9650 sends them), so up by its height.
        const float h = c.images.empty() ? 0.0f : float(assets_.texture(c.images[0], &palette_).h);
        r.b.rect = {x_ + c.x, y_ + c.y - float(n - 1 - i) * h, 0, 0};
        if (c.images.size() >= 2 && !c.values.empty()) {
            // GCtrlTextBtn_v3: two font.mgr ids, normal and pressed.
            const uint16_t fonts[2] = {uint16_t(c.values[0] & 0xFFFF), uint16_t(c.values[0] >> 16)};
            for (int f = 0; f < 2; ++f) {
                const auto img = ui::renderTextButton(assets_.rgba(c.images[size_t(f)], &palette_),
                                                      assets_.font(fonts[f]), palette_, caption);
                r.face[f] = assets_.upload(img.pixels, img.width, img.height);
                r.b.rect.w = float(img.width), r.b.rect.h = float(img.height);
            }
        }
        responses_.push_back(std::move(r));
    }
    if (focus_) focus_ = responses_.empty() ? std::nullopt : std::optional<size_t>(0);

    // Message 0x0b: the NPC's words (mode 2).
    box_ = *text_->textBox;
    const fnt::Font& font = assets_.font(box_.font);
    // GCtrlStaticTextBox_v7, message 0x15 with -n: the box's height less n
    // of its font's lines.
    box_.height = uint16_t(std::max(0, int(box_.height) - int(n) * font.lineHeight));
    words_ = node->text;
    lines_ = ui::wrapText(font, words_, box_.width - 2 * box_.padX);
    visible_ = std::max(1, (int(box_.height) - 2 * int(box_.padY)) / std::max<int>(1, font.lineHeight));
    top_ = 0;
    renderText();

    // The portrait: the top dialog's item (FUN_0049f710 makes it and hands
    // it to the picture slot), its first frame.
    if (story.portrait() != portraitId_) {
        portraitId_ = story.portrait();
        portraitTex_ = nullptr;
        if (portraitId_ && portrait_) try {
                const itm::Item item = assets_.itemHeader(portraitId_);
                const Texture& t = assets_.itemTexture(assets_.itemImagePath(item.image));
                if (t.tex) {
                    const int frames = std::max<int>(1, item.frameCount);
                    portraitTex_ = &t;
                    portraitSrc_ = {0, 0, float(t.w), float(t.h / frames)};
                    portraitDst_ = {x_ + portrait_->x, y_ + portrait_->y, portraitSrc_.w, portraitSrc_.h};
                }
            } catch (const std::exception& ex) {
                SDL_Log("portrait %u: %s", unsigned(portraitId_), ex.what());
            }
    }
}

void Dialogue::renderText() {
    SDL_DestroyTexture(textTex_.tex);
    textTex_ = {};
    // The box draws from its first shown line; the rest are cut off at its
    // bottom (GCtrlStaticTextBox_v3).
    std::string shown;
    for (size_t i = size_t(top_); i < lines_.size(); ++i) {
        if (!shown.empty()) shown += '\n';
        shown += lines_[i];
    }
    const ui::BoxFonts fonts{&assets_.font(box_.font), &assets_.font(box_.fontAlt), &assets_.font(box_.fontStar)};
    const auto img = ui::renderTextBox(fonts, palette_, box_, shown);
    textTex_ = assets_.upload(img.pixels, img.width, img.height);
    textRect_ = {x_ + text_->x, y_ + text_->y, float(img.width), float(img.height)};
    updateArrows();
}

void Dialogue::updateArrows() {
    // GCtrlScrollButton: image 0 (off) while there is nothing to scroll to.
    auto set = [](Button& b, bool on) {
        if (!on) b.button.state = b.button.saved = ui::ButtonState::Disabled;
        else if (b.button.state == ui::ButtonState::Disabled)
            b.button.state = b.button.saved = ui::ButtonState::Normal;
    };
    set(upBtn_, top_ > 0);
    set(downBtn_, top_ + visible_ < int(lines_.size()));
}

void Dialogue::scroll(int lines) {
    const int maxTop = std::max(0, int(lines_.size()) - visible_);
    const int top = std::clamp(top_ + lines, 0, maxTop);
    if (top == top_) return;
    top_ = top;
    renderText();
}

int Dialogue::at(float x, float y) const {
    auto in = [&](const SDL_FRect& r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; };
    for (size_t i = 0; i < responses_.size(); ++i)
        if (in(responses_[i].b.rect)) return int(i);
    if (in(upBtn_.rect)) return kUp;
    if (in(downBtn_.rect)) return kDown;
    return -1;
}

void Dialogue::pointerMove(float x, float y) {
    focus_.reset();
    const int hit = at(x, y);
    for (size_t i = 0; i < responses_.size(); ++i) responses_[i].b.hovered = int(i) == hit;
}

void Dialogue::press(int which, ui::PointerEvent e) {
    if (which == kUp || which == kDown) {
        Button& b = which == kUp ? upBtn_ : downBtn_;
        if (b.button.state == ui::ButtonState::Disabled) return;
        if (b.button.event(e)) {
            sound(Screen::kPressSound);
            scroll(which == kUp ? -1 : 1);
        }
        return;
    }
    if (which < 0 || size_t(which) >= responses_.size()) return;
    // FUN_0049f6d0: the button's click (message 0x18 with its index) picks
    // that response (FUN_004f8000).
    if (responses_[size_t(which)].b.button.event(e)) {
        sound(Screen::kPressSound);
        choice_ = size_t(which);
    }
}

void Dialogue::pointerDown(float x, float y) {
    pointerMove(x, y);
    captured_ = at(x, y);
    press(captured_, ui::PointerEvent::Down);
}

void Dialogue::pointerUp(float x, float y) {
    pointerMove(x, y);
    const int c = captured_;
    captured_ = -1;
    if (c < 0) return;
    press(c, at(x, y) == c ? ui::PointerEvent::Up : ui::PointerEvent::Cancel);
}

void Dialogue::moveFocus(int delta) {
    if (responses_.empty()) return;
    const int n = int(responses_.size());
    // Moving up past the first response, or down past the last, scrolls
    // the words instead while there are more of them (a rebuild choice).
    if (!focus_) {
        focus_ = delta < 0 ? size_t(n - 1) : 0;
    } else {
        const int at = int(*focus_) + delta;
        if (at < 0 || at >= n) scroll(delta * std::max(1, visible_ - 1));
        focus_ = size_t(std::clamp(at, 0, n - 1));
    }
    for (size_t i = 0; i < responses_.size(); ++i) responses_[i].b.hovered = i == *focus_;
}

void Dialogue::activateFocus() {
    if (!focus_ && responses_.size() == 1) focus_ = 0;  // a lone response needs no aiming
    if (!focus_) return;
    press(int(*focus_), ui::PointerEvent::Down);
    press(int(*focus_), ui::PointerEvent::Up);
}

std::optional<size_t> Dialogue::takeChoice() { return std::exchange(choice_, std::nullopt); }

void Dialogue::update(uint64_t nowMs) {
    (void)nowMs;
    if (captured_ >= 0) press(captured_, ui::PointerEvent::Held);
}

void Dialogue::sound(int slot) const {
    if (sound_ && size_t(slot) < itf_.sounds.size() && itf_.sounds[size_t(slot)] != 0)
        sound_(slot, itf_.sounds[size_t(slot)], true);
}

void Dialogue::draw(SDL_Renderer* ren) const {
    auto blit = [&](const Texture* t, const SDL_FRect& r, const SDL_FRect* src = nullptr) {
        if (t && t->tex) SDL_RenderTexture(ren, t->tex, src, &r);
    };
    if (backgroundTex_)
        blit(backgroundTex_, {x_, y_, float(backgroundTex_->w), float(backgroundTex_->h)});
    if (portraitTex_) blit(portraitTex_, portraitDst_, &portraitSrc_);
    blit(&textTex_, textRect_);
    auto arrow = [&](const Button& b, const Texture* const* img) {
        // GCtrlButton_v14: image 1 normally, 2 pressed, 0 disabled.
        const ui::ButtonState st = b.button.state;
        blit(img[st == ui::ButtonState::Pressed ? 2 : st == ui::ButtonState::Disabled ? 0 : 1], b.rect);
    };
    arrow(upBtn_, up_);
    arrow(downBtn_, down_);
    for (const auto& r : responses_)
        blit(&r.face[r.b.button.state == ui::ButtonState::Pressed ? 1 : 0], r.b.rect);
    // Keyboard / pad focus (a rebuild addition), as on the other screens.
    if (focus_ && *focus_ < responses_.size()) {
        const SDL_FRect& r = responses_[*focus_].b.rect;
        const SDL_FRect box{r.x - 2, r.y - 2, r.w + 4, r.h + 4};
        SDL_SetRenderDrawColor(ren, 96, 255, 160, 255);
        SDL_RenderRect(ren, &box);
    }
}

}  // namespace harbinger::app
