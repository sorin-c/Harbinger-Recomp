#include "app/hud.hpp"

#include <algorithm>
#include <cmath>

#include "ui/text.hpp"
#include "world/inventory_layout.hpp"
#include "world/item_text.hpp"

namespace harbinger::app {

namespace {

// HUD control classes (the engine's control factory ids, see itf::className).
enum : uint16_t {
    kHealthBtn = 0x01,
    kInjectorBtn = 0x02,
    kThreeStateBtn = 0x04,
    kHealthBar = 0x06,
    kEnergyBar = 0x07,
    kWarningLight = 0x08,
    kGladHealthBtn = 0x09,
    kGladHealthBar = 0x0A,
    kInventoryGrid = 0x10,
    kHumDropSlot = 0x17,
    kInvThreeStateBtn = 0x24,
    kInventoryEquipMask = 0x26,
    kCulHealthBtn = 0x27,
    kCulHealthBar = 0x28,
    kCulEnergyBar = 0x2A,
    kGladDropSlot = 0x30,
    kStatsThreeStateBtn = 0x4B,
    kQuestLogButton = 0x56,
};

int slotKind(uint16_t cls) { return world::equipSlotKind(cls); }

std::string itfPath(world::Character c, const char* name) {
    return "res\\common\\interface\\" + world::interfaceDir(c) + "\\" + name + ".itf";
}

// FUN_004c4af0: at 1024x768 each panel kind moves from its 800x600 place
// to the matching edge (applied to the background; every other control is
// relative to it). The log buttons' y is scaled by 1.28 there, which gives
// 696 for the Human's; the original draws it at 706 (measured), so 10 more.
// The x moves are the view's growth past 800 wide (224) or half of it (112:
// centred), so the rebuild applies the same rule to a widescreen view (1366
// or 1066 wide): right panels stay on the right edge, centred panels stay
// centred. The y moves belong to the height (768 or 600), which widescreen
// keeps.
SDL_FPoint panelPosition(uint16_t kind, int x, int y, int screenW, int screenH) {
    const int dx = screenW - 800, dy = screenH - 600;
    switch (kind) {
        case 0x00: case 0x10: return {float(x), float(y + dy)};
        case 0x01: case 0x0A: case 0x12: return {float(x + dx), float(y + dy)};
        case 0x03: return {float(x + dx), float(y)};
        case 0x05: case 0x06: case 0x13: case 0x0B: case 0x0D: case 0x0E: case 0x0F:
        case 0x15: case 0x16: case 0x17: case 0x19: return {float(x + dx / 2), float(y + (dy ? 0x5E : 0))};
        case 0x1A: return {float(x + dx), screenH >= 768 ? float(int(y * 1.28) + 10) : float(y)};
        default: return {float(x), float(y)};
    }
}

// Grid cells are 26 pixels apart and 25 wide (GCtrlInventoryGrid_v3).
constexpr int kCell = 26, kCellArt = 25;
// The grid's fills: cells an item covers, and the cells a held item would
// take where it fits (both read from the original's frames); where it would
// swap or does not fit (not seen: guessed).
constexpr SDL_Color kOccupied{1, 1, 84, 255}, kFits{0, 97, 50, 255}, kSwaps{97, 97, 0, 255}, kBlocked{97, 0, 0, 255};
// Rebuild: the pad focus frame.
constexpr SDL_Color kFocus{120, 230, 140, 255};

// The warning light comes on under 21% health (0x5506d8) and blinks every
// 100 ms; the health bar moves 0.25 of the way a second (GCtrlHealthBar_v3)
// and never shows less than 5% while alive (GCtrlHealthBar_v7).
constexpr double kWarning = 0.21, kBarSpeed = 0.25, kBarMin = 0.05;
constexpr uint64_t kBlinkMs = 100, kHealthTipDelayMs = 500;
// Rebuild: stick repeat for the pad focus.
constexpr uint64_t kStickFirstMs = 280, kStickRepeatMs = 120;
constexpr float kStickThreshold = 0.5f;

// Inventory sound slots: take, put, swap (GCtrlInventoryGrid_v7 plays 3
// for a put and 4 for a swap; 2 for a take is from the order of the
// .itf's slots: likely).
enum { kTakeSound = 2, kPutSound = 3, kSwapSound = 4 };

void over(render::RgbaImage& dst, const render::RgbaImage& src, int x, int y, int w = -1, int h = -1) {
    if (w < 0) w = src.width;
    if (h < 0) h = src.height;
    for (int sy = 0; sy < h && sy < src.height; ++sy)
        for (int sx = 0; sx < w && sx < src.width; ++sx) {
            const int dx = x + sx, dy = y + sy;
            if (dx < 0 || dy < 0 || dx >= dst.width || dy >= dst.height) continue;
            const uint32_t p = src.pixels[size_t(sy) * size_t(src.width) + size_t(sx)];
            if (p >> 24) dst.pixels[size_t(dy) * size_t(dst.width) + size_t(dx)] = p;
        }
}

void fill(SDL_Renderer* ren, const SDL_FRect& r, SDL_Color c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(ren, &r);
}

}  // namespace

bool Hud::Panel::hit(float px, float py) const {
    const int x = int(std::floor(px - this->x)), y = int(std::floor(py - this->y));
    if (x < 0 || y < 0 || x >= mask.width || y >= mask.height) return false;
    return mask.pixels[size_t(y) * size_t(mask.width) + size_t(x)] >> 24;
}

Hud::Hud(Assets& assets, world::Character character, int screenW, int screenH, Screen::SoundFn sound)
    : assets_(assets), character_(character), screenW_(screenW), screenH_(screenH), sound_(std::move(sound)) {
    load(left_, itfPath(character, "left"));
    load(right_, itfPath(character, "right"));
    load(log_, itfPath(character, "logbuttons"));
    load(inv_, itfPath(character, "inventory"));
    load(popup_, "res\\common\\interface\\descpopup\\descpopup.itf");
}

Hud::~Hud() {
    SDL_DestroyTexture(tip_.tex);
    SDL_DestroyTexture(money_.tex);
}

void Hud::load(Panel& p, const std::string& path) {
    p.itf = itf::parse(assets_.read(path));
    const itf::Control* bg = nullptr;
    for (const auto& c : p.itf.controls)
        if (c.classId == itf::Backgd && !c.images.empty()) {
            bg = &c;
            break;
        }
    if (bg) {
        // As in the front end, the panel's art and text take the background's palette.
        p.palette = assets_.image(bg->images[0]).palette;
        p.mask = assets_.rgba(bg->images[0], &p.palette);
        const SDL_FPoint at = panelPosition(p.itf.kind, bg->x, bg->y, screenW_, screenH_);
        p.x = at.x, p.y = at.y;
    }
    int slot = 0;
    for (const auto& c : p.itf.controls) {
        Control w;
        w.control = &c;
        for (const auto& img : c.images) w.images.push_back(&assets_.texture(img, &p.palette));
        const float x = c.classId == itf::Backgd ? p.x : p.x + c.x, y = c.classId == itf::Backgd ? p.y : p.y + c.y;
        w.rect = {x, y, 0, 0};
        if (!w.images.empty()) w.rect.w = float(w.images[0]->w), w.rect.h = float(w.images[0]->h);
        if (slotKind(c.classId) >= 0) {
            w.slot = slot++;
            // The Culibine's gauntlet slots have no image: a gauntlet-sized box (guessed).
            if (w.images.empty()) w.rect.w = 90, w.rect.h = 90;
        }
        p.controls.push_back(w);
    }
    // The background first, then higher z first (as Screen does).
    std::stable_sort(p.controls.begin(), p.controls.end(), [](const Control& a, const Control& b) {
        const bool ba = a.control->classId == itf::Backgd, bb = b.control->classId == itf::Backgd;
        if (ba != bb) return ba;
        return a.control->z > b.control->z;
    });
}

world::Inventory::Layout Hud::inventoryLayout(Assets& assets, world::Character character) {
    return world::inventoryLayout(itf::parse(assets.read(itfPath(character, "inventory"))), character);
}

void Hud::attach(world::Inventory* inventory, world::ItemCatalog* catalog) {
    inventory_ = inventory;
    catalog_ = catalog;
}

bool Hud::setInventoryOpen(bool open) {
    if (open == open_) return true;
    if (!open && inventory_ && inventory_->held() && !inventory_->stowHeld()) return false;
    open_ = open;
    if (open) focus_ = {}, clampFocus();
    setTooltip({}, 0, 0, 0);
    return true;
}

double Hud::viewCentreX() const {
    // The world centres on the part the inventory leaves free: the player
    // stands at x 270 of 1024 in the original with it open.
    return open_ ? inv_.x / 2.0 : screenW_ / 2.0;
}

const Hud::Control* Hud::find(const Panel& p, uint16_t cls) const {
    for (const auto& c : p.controls)
        if (c.control->classId == cls) return &c;
    return nullptr;
}

bool Hud::covers(float x, float y) const {
    if (open_ && inv_.hit(x, y)) return true;
    return left_.hit(x, y) || right_.hit(x, y) || log_.hit(x, y);
}

// --- inventory geometry -------------------------------------------------

SDL_FRect Hud::cellRect(int cx, int cy) const {
    const Control* g = find(inv_, kInventoryGrid);
    const float x = g ? g->rect.x : 0, y = g ? g->rect.y : 0;
    return {x + float(cx * kCell), y + float(cy * kCell), float(kCellArt), float(kCellArt)};
}

std::optional<std::pair<int, int>> Hud::cellAt(float x, float y) const {
    if (!inventory_) return std::nullopt;
    const Control* g = find(inv_, kInventoryGrid);
    if (!g) return std::nullopt;
    const int cx = int(std::floor((x - g->rect.x) / kCell)), cy = int(std::floor((y - g->rect.y) / kCell));
    if (cx < 0 || cy < 0 || cx >= inventory_->gridW() || cy >= inventory_->gridH()) return std::nullopt;
    return std::make_pair(cx, cy);
}

int Hud::slotAt(float x, float y) const {
    if (!inventory_) return -1;
    for (const auto& c : inv_.controls)
        if (c.slot >= 0 && size_t(c.slot) < inventory_->slotCount() && inventory_->slotEnabled(size_t(c.slot)) &&
            x >= c.rect.x && x < c.rect.x + c.rect.w && y >= c.rect.y && y < c.rect.y + c.rect.h)
            return c.slot;
    return -1;
}

bool Hud::maskAt(float x, float y) const {
    for (const auto& c : inv_.controls)
        if (c.control->classId == kInventoryEquipMask && !c.images.empty() && x >= c.rect.x &&
            x < c.rect.x + c.rect.w && y >= c.rect.y && y < c.rect.y + c.rect.h)
            return true;
    return false;
}

SDL_FRect Hud::focusRect(const Focus& f) const {
    if (f.slot >= 0)
        for (const auto& c : inv_.controls)
            if (c.slot == f.slot) return c.rect;
    return cellRect(f.cx, f.cy);
}

// --- pointer ------------------------------------------------------------

void Hud::pointerMove(float x, float y) {
    if (x != mouseX_ || y != mouseY_) hoverSince_ = now_;
    if (pad_ && (std::abs(x - mouseX_) > 2 || std::abs(y - mouseY_) > 2) && mouseX_ >= 0) pad_ = false;
    mouseX_ = x, mouseY_ = y;
}

bool Hud::pointerDown(float x, float y) {
    pad_ = false;
    pointerMove(x, y);
    // The panel buttons.
    if (const Control* c = find(right_, kInvThreeStateBtn);
        c && x >= c->rect.x && x < c->rect.x + c->rect.w && y >= c->rect.y && y < c->rect.y + c->rect.h) {
        toggleInventory();
        sound(5);
        return true;
    }
    if (const Control* c = find(right_, kThreeStateBtn);
        c && x >= c->rect.x && x < c->rect.x + c->rect.w && y >= c->rect.y && y < c->rect.y + c->rect.h) {
        actions_.push_back(Action::Map);
        return true;
    }
    for (uint16_t cls : {kHealthBtn, kGladHealthBtn, kCulHealthBtn})
        if (const Control* c = find(left_, cls);
            c && x >= c->rect.x && x < c->rect.x + c->rect.w && y >= c->rect.y && y < c->rect.y + c->rect.h) {
            actions_.push_back(Action::UseHealthPack);
            return true;
        }
    if (log_.hit(x, y)) {
        actions_.push_back(Action::QuestLog);
        return true;
    }
    if (open_ && inv_.hit(x, y)) {
        clickInventory(x, y);
        return true;
    }
    // Holding an item, a click outside the screen does nothing (the
    // original has no dropping on the floor from the inventory: not seen).
    if (open_ && inventory_ && inventory_->held()) return true;
    return covers(x, y);
}

bool Hud::pointerAlt(float x, float y) {
    pad_ = false;
    pointerMove(x, y);
    if (!open_ || !inv_.hit(x, y) || !inventory_ || inventory_->held()) return covers(x, y);
    if (const auto cell = cellAt(x, y)) quickMove(Focus{-1, cell->first, cell->second});
    else if (const int s = slotAt(x, y); s >= 0) quickMove(Focus{s, 0, 0});
    return true;
}

void Hud::clickInventory(float x, float y) {
    if (!inventory_) return;
    if (const auto cell = cellAt(x, y)) return clickCell(cell->first, cell->second);
    if (const int s = slotAt(x, y); s >= 0) return clickSlot(s);
    // GCtrlInventoryEquipMask: holding an item over the top of the screen
    // hands the click to the slot that takes it (an empty one first).
    if (inventory_->held() && maskAt(x, y)) {
        const uint16_t id = inventory_->held()->id;
        int best = -1;
        for (size_t i = 0; i < inventory_->slotCount(); ++i)
            if (inventory_->accepts(i, id) && (best < 0 || !inventory_->equipped(i))) {
                if (best < 0 || (inventory_->equipped(size_t(best)) && !inventory_->equipped(i))) best = int(i);
            }
        if (best >= 0) clickSlot(best);
    }
}

void Hud::clickSlot(int slot) {
    switch (inventory_->clickSlot(size_t(slot))) {
        case world::Inventory::Click::Took: sound(kTakeSound); break;
        case world::Inventory::Click::Put: sound(kPutSound); break;
        case world::Inventory::Click::Swapped: sound(kSwapSound); break;
        default: break;
    }
}

void Hud::clickCell(int cx, int cy) {
    switch (inventory_->clickGrid(cx, cy)) {
        case world::Inventory::Click::Took: sound(kTakeSound); break;
        case world::Inventory::Click::Put: sound(kPutSound); break;
        case world::Inventory::Click::Swapped: sound(kSwapSound); break;
        default: break;
    }
}

// Rebuild (D3's quick equip): a grid item goes to the first slot that
// takes it, preferring an empty one, and what it replaces goes to the grid;
// an equipped item that may come off goes to the grid.
void Hud::quickMove(std::optional<Focus> at) {
    if (!inventory_ || !at || inventory_->held()) return;
    if (at->slot >= 0) {
        const size_t s = size_t(at->slot);
        if (!inventory_->equipped(s) || world::Inventory::locked(inventory_->slot(s).kind)) return;
        if (inventory_->clickSlot(s) != world::Inventory::Click::Took) return;
        if (inventory_->stowHeld()) sound(kPutSound);
        else inventory_->clickSlot(s);  // no room: put it back
        return;
    }
    const int k = inventory_->itemAt(at->cx, at->cy);
    if (k < 0) return;
    const auto placed = inventory_->grid()[size_t(k)];
    int best = -1;
    for (size_t i = 0; i < inventory_->slotCount(); ++i)
        if (inventory_->accepts(i, placed.item.id) &&
            (best < 0 || (inventory_->equipped(size_t(best)) && !inventory_->equipped(i))))
            best = int(i);
    if (best < 0) return;
    inventory_->clickGrid(placed.x, placed.y);
    const auto r = inventory_->clickSlot(size_t(best));
    if (r == world::Inventory::Click::Swapped && !inventory_->stowHeld()) {
        // The old item has no room in the grid: undo.
        inventory_->clickSlot(size_t(best));
        inventory_->stowHeld();
        return;
    }
    sound(r == world::Inventory::Click::Swapped ? kSwapSound : kPutSound);
}

// --- pad ----------------------------------------------------------------

void Hud::clampFocus() {
    if (!inventory_) return;
    if (focus_.slot >= 0 && (size_t(focus_.slot) >= inventory_->slotCount() || !inventory_->slotEnabled(size_t(focus_.slot))))
        focus_.slot = -1;
    focus_.cx = std::clamp(focus_.cx, 0, inventory_->gridW() - 1);
    focus_.cy = std::clamp(focus_.cy, 0, inventory_->gridH() - 1);
}

void Hud::padMove(int dx, int dy) {
    if (!open_ || !inventory_ || (dx == 0 && dy == 0)) return;
    pad_ = true;
    // Spatial navigation over the enabled slots and the grid cells: the
    // nearest target whose centre lies in the pressed direction, weighing
    // sideways distance double.
    const SDL_FRect from = focusRect(focus_);
    const float fx = from.x + from.w / 2, fy = from.y + from.h / 2;
    std::optional<Focus> best;
    float bestScore = 0;
    auto consider = [&](const Focus& f) {
        const SDL_FRect r = focusRect(f);
        const float ox = r.x + r.w / 2 - fx, oy = r.y + r.h / 2 - fy;
        const float along = ox * float(dx) + oy * float(dy);
        const float across = std::abs(ox * float(dy)) + std::abs(oy * float(dx));
        if (along <= 1) return;
        const float score = along + 2 * across;
        if (!best || score < bestScore) best = f, bestScore = score;
    };
    for (size_t i = 0; i < inventory_->slotCount(); ++i)
        if (inventory_->slotEnabled(i) && int(i) != focus_.slot) consider(Focus{int(i), focus_.cx, focus_.cy});
    for (int y = 0; y < inventory_->gridH(); ++y)
        for (int x = 0; x < inventory_->gridW(); ++x)
            if (focus_.slot >= 0 || x != focus_.cx || y != focus_.cy) consider(Focus{-1, x, y});
    if (best) focus_ = *best;
    setTooltip({}, 0, 0, 0);
}

void Hud::padStick(float x, float y) {
    const bool was = std::abs(stickX_) >= kStickThreshold || std::abs(stickY_) >= kStickThreshold;
    stickX_ = x, stickY_ = y;
    const bool is = std::abs(x) >= kStickThreshold || std::abs(y) >= kStickThreshold;
    if (is && !was) {
        padMove(std::abs(x) >= std::abs(y) ? (x > 0 ? 1 : -1) : 0, std::abs(y) > std::abs(x) ? (y > 0 ? 1 : -1) : 0);
        stickNext_ = now_ + kStickFirstMs;
    }
}

void Hud::padPress() {
    if (!open_ || !inventory_) return;
    pad_ = true;
    clampFocus();
    if (focus_.slot >= 0) clickSlot(focus_.slot);
    else clickCell(focus_.cx, focus_.cy);
}

void Hud::padQuick() {
    if (!open_ || !inventory_) return;
    pad_ = true;
    quickMove(focus_);
}

std::vector<Hud::Action> Hud::takeActions() {
    std::vector<Action> out;
    out.swap(actions_);
    return out;
}

void Hud::sound(int slot) const {
    if (!sound_) return;
    const auto& s = slot == 5 ? right_.itf.sounds : inv_.itf.sounds;
    if (size_t(slot) < s.size() && s[size_t(slot)]) sound_(slot, s[size_t(slot)], true);
}

// --- update -------------------------------------------------------------

void Hud::update(uint64_t nowMs, const Vitals& vitals) {
    const double dt = lastUpdate_ ? double(nowMs - lastUpdate_) / 1000.0 : 0;
    lastUpdate_ = now_ = nowMs;
    vitals_ = vitals;
    double target = vitals.maxHitPoints > 0 ? std::clamp(vitals.hitPoints / vitals.maxHitPoints, 0.0, 1.0) : 0;
    if (target > 0 && target < kBarMin) target = kBarMin;
    if (shownHealth_ < target) shownHealth_ = std::min(target, shownHealth_ + kBarSpeed * dt);
    else if (shownHealth_ > target) shownHealth_ = std::max(target, shownHealth_ - kBarSpeed * dt);

    // Stick repeat.
    if ((std::abs(stickX_) >= kStickThreshold || std::abs(stickY_) >= kStickThreshold) && now_ >= stickNext_ && stickNext_) {
        padMove(std::abs(stickX_) >= std::abs(stickY_) ? (stickX_ > 0 ? 1 : -1) : 0,
                std::abs(stickY_) > std::abs(stickX_) ? (stickY_ > 0 ? 1 : -1) : 0);
        stickNext_ = now_ + kStickRepeatMs;
    }

    // The description popup: the item under the pointer (or the pad focus),
    // or the health button's count after a moment.
    std::string text;
    float ax = 0, ay = 0, ab = 0;
    if (inventory_) {
        if (open_ && !inventory_->held()) {
            std::optional<world::Carried> item;
            SDL_FRect r{};
            std::optional<std::pair<int, int>> cell;
            int s = -1;
            if (pad_) {
                if (focus_.slot >= 0) s = focus_.slot;
                else cell = std::make_pair(focus_.cx, focus_.cy);
            } else {
                cell = cellAt(mouseX_, mouseY_);
                if (!cell) s = slotAt(mouseX_, mouseY_);
            }
            if (cell) {
                const int k = inventory_->itemAt(cell->first, cell->second);
                if (k >= 0) {
                    const auto& p = inventory_->grid()[size_t(k)];
                    item = p.item;
                    const world::ItemInfo* info = catalog_ ? catalog_->info(p.item.id) : nullptr;
                    const SDL_FRect a = cellRect(p.x, p.y);
                    r = {a.x, a.y, float((info ? info->w : 1) * kCell), float((info ? info->h : 1) * kCell)};
                }
            } else if (s >= 0) {
                item = inventory_->equipped(size_t(s));
                if (item && inventory_->slot(size_t(s)).parent < 0) item->chips = inventory_->chipsOf(size_t(s));
                r = focusRect(Focus{s, 0, 0});
            }
            if (item && catalog_) {
                text = catalog_->description(*item);
                ax = r.x + r.w / 2, ay = r.y, ab = r.y + r.h;
            }
        }
        if (text.empty() && !pad_)
            for (uint16_t cls : {kHealthBtn, kGladHealthBtn, kCulHealthBtn})
                if (const Control* c = find(left_, cls);
                    c && mouseX_ >= c->rect.x && mouseX_ < c->rect.x + c->rect.w && mouseY_ >= c->rect.y &&
                    mouseY_ < c->rect.y + c->rect.h && now_ - hoverSince_ >= kHealthTipDelayMs) {
                    text = world::healthPackText(inventory_->healthPacks());
                    ax = c->rect.x + c->rect.w / 2, ay = c->rect.y, ab = c->rect.y + c->rect.h;
                }
    }
    setTooltip(text, ax, ay, ab);

    // The money box.
    if (inventory_ && inventory_->money() != moneyShown_) {
        moneyShown_ = inventory_->money();
        SDL_DestroyTexture(money_.tex);
        money_ = {};
        for (const auto& c : inv_.controls)
            if (c.control->classId == itf::StaticTextBox && c.control->textBox) {
                const itf::TextBox& b = *c.control->textBox;
                const ui::BoxFonts fonts{&assets_.font(b.font), &assets_.font(b.fontAlt), &assets_.font(b.fontStar)};
                const auto img = ui::renderTextBox(fonts, inv_.palette, b, std::to_string(moneyShown_));
                money_ = assets_.upload(img.pixels, img.width, img.height);
            }
    }
}

void Hud::setTooltip(const std::string& text, float anchorX, float anchorY, float bottomY) {
    if (text == tipText_) return;
    tipText_ = text;
    SDL_DestroyTexture(tip_.tex);
    tip_ = {};
    if (text.empty() || popup_.controls.size() < 2) return;
    // FUN_004f5470 / FUN_004f5520: the popup is the text's size plus
    // (22, 18), at most 512 high; its background is Backgd cropped to that
    // size with Screen inset by one pixel, and the text box's fonts.
    const Control* bg = nullptr;
    const Control* box = nullptr;
    for (const auto& c : popup_.controls) {
        if (c.control->classId == itf::Backgd) bg = &c;
        else if (c.control->textBox) box = &c;
    }
    if (!bg || !box) return;
    itf::TextBox b = *box->control->textBox;
    const ui::BoxFonts fonts{&assets_.font(b.font), &assets_.font(b.fontAlt), &assets_.font(b.fontStar)};
    const auto lines = ui::wrapText(*fonts.normal, text, b.width);
    int textW = 0;
    for (std::string_view line : lines) {
        const fnt::Font* f = fonts.normal;
        if (!line.empty() && line[0] == '*') f = fonts.star, line.remove_prefix(1);
        if (!line.empty() && line[0] == '&') f = fonts.alt, line.remove_prefix(1);
        textW = std::max(textW, ui::measure(*f, line).width);
    }
    const int w = textW + 22, h = std::min(int(lines.size()) * fonts.normal->lineHeight + 18, 512);
    render::RgbaImage img{w, h, std::vector<uint32_t>(size_t(w) * size_t(h), 0)};
    const auto back = assets_.rgba(bg->control->images[0], &popup_.palette);
    over(img, back, 0, 0, w, h);
    if (!box->control->images.empty()) over(img, assets_.rgba(box->control->images[0], &popup_.palette), 1, 1, w - 2, h - 2);
    b.width = uint16_t(w - 2), b.height = uint16_t(h - 2);
    over(img, ui::renderTextBox(fonts, popup_.palette, b, text), 1, 1);
    tip_ = assets_.upload(img.pixels, img.width, img.height);
    // Centred over the anchor and kept on screen; below it if there is no
    // room above.
    float x = std::clamp(anchorX - float(w) / 2, 0.0f, float(screenW_ - w - 1));
    float y = anchorY - float(h);
    if (y < 0) y = bottomY;
    y = std::clamp(y, 0.0f, float(screenH_ - h));
    tipRect_ = {std::floor(x), std::floor(y), float(w), float(h)};
}

// --- drawing ------------------------------------------------------------

const Texture* Hud::itemTexture(uint16_t id, SDL_FRect& src) const {
    if (!catalog_) return nullptr;
    const itm::Item* item = catalog_->item(id);
    if (!item) return nullptr;
    try {
        const Texture& t = assets_.itemTexture(assets_.itemImagePath(item->image));
        if (!t.tex) return nullptr;
        // The image holds frameCount pictures stacked; the first is the
        // inventory's.
        src = {0, 0, float(t.w), float(t.h / std::max<int>(1, item->frameCount))};
        return &t;
    } catch (const std::exception&) {
        return nullptr;
    }
}

void Hud::drawItem(SDL_Renderer* ren, uint16_t id, float x, float y, bool centred) const {
    SDL_FRect src;
    const Texture* t = itemTexture(id, src);
    if (!t) return;
    SDL_FRect dst{x, y, src.w, src.h};
    if (centred) dst.x = std::floor(x - src.w / 2), dst.y = std::floor(y - src.h / 2);
    SDL_RenderTexture(ren, t->tex, &src, &dst);
}

void Hud::drawControl(SDL_Renderer* ren, const Panel& p, const Control& c) const {
    (void)p;
    auto image = [&](size_t i, const SDL_FRect* src = nullptr, const SDL_FRect* dst = nullptr) {
        if (i >= c.images.size()) return;
        const Texture* t = c.images[i];
        const SDL_FRect whole{c.rect.x, c.rect.y, float(t->w), float(t->h)};
        SDL_RenderTexture(ren, t->tex, src, dst ? dst : &whole);
    };
    const double health = shownHealth_;
    const double energy = vitals_.maxEnergy > 0 ? std::clamp(vitals_.energy / vitals_.maxEnergy, 0.0, 1.0) : 0;
    const uint16_t cls = c.control->classId;
    switch (cls) {
        case itf::Backgd: image(0); break;
        case kHealthBtn:
        case kGladHealthBtn:
        case kCulHealthBtn: {
            // GCtrlHealthBtn_v3: the bottle and, over it, the count's digit
            // (the Human's digits start at image 3, after three bottles; the
            // others' right after their one picture: likely).
            const int count = inventory_ ? inventory_->healthPacks() : 0;
            if (count <= 0) break;
            image(0);
            image(size_t(std::min(count, 10) + (cls == kHealthBtn ? 2 : 1)));
            break;
        }
        case kHealthBar:
        case kGladHealthBar: {
            // GCtrlHealthBar_v3: a window half the slider's width slides
            // along it with the fraction, so the red end shows at full
            // health and the empty spring at none.
            if (c.images.empty()) break;
            const Texture* t = c.images[0];
            // The Gladiator's slider has its fill on the left half and the
            // empty tube on the right, the Human's the other way round, so
            // the window moves the other way (read from the art: likely).
            const float w = float(t->w / 2);
            const float l = cls == kHealthBar ? std::floor(float(health) * w) : std::floor(float(1 - health) * w);
            const SDL_FRect src{l, 0, w, float(t->h)}, dst{c.rect.x, c.rect.y, w, float(t->h)};
            SDL_RenderTexture(ren, t->tex, &src, &dst);
            break;
        }
        case kCulHealthBar: {
            // Not traced: the meter cut to the fraction from the left (guessed).
            if (c.images.empty()) break;
            const Texture* t = c.images[0];
            const float w = std::floor(float(health) * float(t->w));
            const SDL_FRect src{0, 0, w, float(t->h)}, dst{c.rect.x, c.rect.y, w, float(t->h)};
            SDL_RenderTexture(ren, t->tex, &src, &dst);
            break;
        }
        case kEnergyBar:
        case kCulEnergyBar: {
            // GCtrlEnergyBar_v3: the meter's rows from height * (1 - fraction) down.
            if (c.images.empty()) break;
            const Texture* t = c.images[0];
            const float top = float(t->h) - std::floor(float(energy) * float(t->h));
            const SDL_FRect src{0, top, float(t->w), float(t->h) - top}, dst{c.rect.x, c.rect.y + top, float(t->w), float(t->h) - top};
            SDL_RenderTexture(ren, t->tex, &src, &dst);
            break;
        }
        case kWarningLight:
            if (vitals_.maxHitPoints > 0 && vitals_.hitPoints / vitals_.maxHitPoints < kWarning && (now_ / kBlinkMs) % 2 == 0)
                image(0);
            break;
        case kInvThreeStateBtn:
            // GCtrlThreeStateBtn: nothing while off, image 1 while on.
            if (open_) image(1);
            break;
        case kInjectorBtn:
            // An injector in its slot shows a full bottle (the colour and
            // fill level by injector are not decoded yet).
            if (inventory_)
                for (size_t i = 0; i < inventory_->slotCount(); ++i)
                    if (inventory_->slot(i).kind == 8 && inventory_->equipped(i)) image(0);
            break;
        case itf::StaticTextBox:
            if (money_.tex) {
                const SDL_FRect dst{c.rect.x, c.rect.y, float(money_.w), float(money_.h)};
                SDL_RenderTexture(ren, money_.tex, nullptr, &dst);
            }
            break;
        case kInventoryGrid:
            image(0);  // the cells; the items are drawn by drawInventory
            break;
        default:
            if (c.slot >= 0 && inventory_ && size_t(c.slot) < inventory_->slotCount()) {
                // GCtrlEquipSlot_v3: an enabled slot draws its picture and
                // its item centred on it; a disabled one shows the background.
                const size_t s = size_t(c.slot);
                if (!inventory_->slotEnabled(s)) break;
                image(0);
                if (const auto item = inventory_->equipped(s))
                    drawItem(ren, item->id, c.rect.x + c.rect.w / 2, c.rect.y + c.rect.h / 2, true);
            }
            break;
    }
}

void Hud::drawPanel(SDL_Renderer* ren, const Panel& p) const {
    for (const auto& c : p.controls) drawControl(ren, p, c);
}

void Hud::drawInventory(SDL_Renderer* ren) const {
    drawPanel(ren, inv_);
    if (!inventory_ || !catalog_) return;
    // GCtrlInventoryGrid_v3: each covered cell filled dark blue inside a
    // one-pixel edge, then the items from their top-left cells.
    for (const auto& p : inventory_->grid()) {
        const world::ItemInfo* info = catalog_->info(p.item.id);
        if (!info) continue;
        for (int y = 0; y < info->h; ++y)
            for (int x = 0; x < info->w; ++x)
                if (info->covers(x, y)) {
                    const SDL_FRect r = cellRect(p.x + x, p.y + y);
                    fill(ren, {r.x + 1, r.y + 1, r.w - 2, r.h - 2}, kOccupied);
                }
    }
    // Where the held item would go.
    const auto& held = inventory_->held();
    std::optional<std::pair<int, int>> at;
    if (held) at = pad_ ? std::optional(std::make_pair(focus_.cx, focus_.cy)) : cellAt(mouseX_, mouseY_);
    if (held && at && !(pad_ && focus_.slot >= 0)) {
        if (const world::ItemInfo* info = catalog_->info(held->id)) {
            const auto [x0, y0] = inventory_->dropCell(held->id, at->first, at->second);
            const auto fit = inventory_->fit(held->id, x0, y0);
            const SDL_Color c = fit == world::Inventory::Fit::Free ? kFits : fit == world::Inventory::Fit::Swap ? kSwaps : kBlocked;
            for (int y = 0; y < info->h; ++y)
                for (int x = 0; x < info->w; ++x)
                    if (info->covers(x, y) && x0 + x < inventory_->gridW() && y0 + y < inventory_->gridH()) {
                        const SDL_FRect r = cellRect(x0 + x, y0 + y);
                        fill(ren, {r.x + 1, r.y + 1, r.w - 2, r.h - 2}, c);
                    }
        }
    }
    for (const auto& p : inventory_->grid()) {
        const SDL_FRect r = cellRect(p.x, p.y);
        drawItem(ren, p.item.id, r.x, r.y, false);
    }
    // Rebuild: the pad focus.
    if (pad_) {
        const SDL_FRect r = focusRect(focus_);
        SDL_SetRenderDrawColor(ren, kFocus.r, kFocus.g, kFocus.b, kFocus.a);
        const SDL_FRect a{r.x - 2, r.y - 2, r.w + 4, r.h + 4}, b{r.x - 1, r.y - 1, r.w + 2, r.h + 2};
        SDL_RenderRect(ren, &a);
        SDL_RenderRect(ren, &b);
    }
}

void Hud::draw(SDL_Renderer* ren) const {
    if (open_) drawInventory(ren);
    drawPanel(ren, log_);
    drawPanel(ren, left_);
    drawPanel(ren, right_);
    // The item on the pointer (or, on the pad, over the focus).
    if (inventory_ && inventory_->held()) {
        float x = mouseX_, y = mouseY_;
        if (pad_) {
            const SDL_FRect r = focusRect(focus_);
            x = r.x + r.w / 2, y = r.y + r.h / 2;
        }
        drawItem(ren, inventory_->held()->id, x, y, true);
    }
    if (tip_.tex) SDL_RenderTexture(ren, tip_.tex, nullptr, &tipRect_);
}

}  // namespace harbinger::app
