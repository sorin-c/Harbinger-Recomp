#include "app/cutscene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "ui/text.hpp"

namespace harbinger::app {

namespace {

// Sound keys (SoundSystem::play): one for the bubble's voice line, and one
// per sound.mgr id for Sound commands, clear of the interface's slots.
constexpr int kVoiceKey = 0x10000;
constexpr int kSoundKey = 0x20000;

// res\common\interface\thoughtbub\thoughtbub0.itf: Backgd.tga, then a
// StaticTextBox at (1, 1) on Screen.tga with fonts 7/7/11 and padding 10, 8.
const char* const kBubbleBackground = "Res\\Common\\interface\\ThoughtBub\\Backgd.tga";
const char* const kBubbleScreen = "Res\\Common\\interface\\ThoughtBub\\Screen.tga";
constexpr uint16_t kBubbleFont = 7;
constexpr int kBubblePadX = 10, kBubblePadY = 8;

mgr::Index readIndex(Assets& assets, const char* section, const char* key) {
    try {
        return mgr::parse(assets.read(assets.ini().require(section, key)));
    } catch (const std::exception& ex) {
        SDL_Log("%s %s: %s", section, key, ex.what());
        return {};
    }
}

uint64_t pcmMs(const audio::Pcm& pcm) {
    if (pcm.rate <= 0 || pcm.channels <= 0) return 0;
    return uint64_t(pcm.samples.size()) * 1000 / uint64_t(pcm.rate) / uint64_t(pcm.channels);
}

// The shake word values (names at 0x55797c): None, Disp, Rndm.
int shakeMode(const std::string& word) { return word == "Disp" ? 1 : word == "Rndm" ? 2 : 0; }

// FUN_004a2cf0: Disp holds the amplitude, Rndm picks rand % amp - amp / 2.
int shakeOffset(int mode, int amp) {
    if (mode == 1) return amp;
    if (mode == 2 && amp != 0) return int(unsigned(std::rand()) % unsigned(std::abs(amp))) - amp / 2;
    return 0;
}

}  // namespace

Cutscene::Cutscene(Assets& assets, SDL_Renderer* ren, world::Character character, world::Level& level, int screenW,
                   int screenH)
    : assets_(assets), ren_(ren), level_(level), screenW_(screenW), screenH_(screenH),
      scripts_(readIndex(assets, world::characterSection(character), "Scripts")),
      speech_(readIndex(assets, world::characterSection(character), "Speech")), triggers_(level.map()),
      gizmoTriggers_(level.map()) {}

Cutscene::~Cutscene() {
    stopPcm(kVoiceKey);
    SDL_DestroyTexture(bubble_.face.tex);
}

std::shared_ptr<const gsc::Script> Cutscene::script(uint16_t id) {
    if (auto it = loaded_.find(id); it != loaded_.end()) return it->second;
    std::shared_ptr<const gsc::Script> s;
    try {
        if (const std::string* path = scripts_.find(id)) {
            const auto bytes = assets_.read(*path);
            s = std::make_shared<gsc::Script>(
                gsc::parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())));
        }
    } catch (const std::exception& ex) {
        SDL_Log("script %u: %s", unsigned(id), ex.what());
    }
    return loaded_[id] = s;
}

void Cutscene::fire(const chk::EventTrigger& t, uint64_t nowMs) {
    if (std::getenv("HARBINGER_SCRIPT_LOG"))
        std::fprintf(stderr, "%llu ms: trigger %u fires, script %u\n", (unsigned long long)nowMs, unsigned(t.id),
                     unsigned(t.script));
    // FUN_0048c2c0: the trigger's gizmos are activated (GIActivatable slot 1;
    // breakers, spawners and bridges in the shipped levels), then its script
    // starts, replacing any that is running (FUN_005025a0).
    for (const auto& [instance, data] : t.gizmos)
        if (const chk::GizmoRecord* g = level_.gizmoByInstance(instance)) level_.useGizmo(*g, nowMs, *occupied_);
    if (t.script)
        if (auto s = script(t.script)) runner_.start(std::move(s));
}

void Cutscene::fire(const chk::GizmoTrigger& t, uint64_t nowMs) {
    if (std::getenv("HARBINGER_SCRIPT_LOG"))
        std::fprintf(stderr, "%llu ms: gizmo trigger %u fires, gizmo %u\n", (unsigned long long)nowMs, unsigned(t.id),
                     unsigned(t.gizmo));
    // GGizmoTrigger_v3: the gizmo's GIActivatable slot 1, with the
    // trigger's id.
    const chk::GizmoRecord* g = level_.gizmoByInstance(t.gizmo);
    if (!g) return;
    if (const auto link = world::portalLink(level_.gizmoTemplate(*g), *g)) runPortal(*g, *link, t.id, nowMs);
    else level_.useGizmo(*g, nowMs, *occupied_);
}

void Cutscene::runPortal(const chk::GizmoRecord& g, const world::PortalLink& link, uint16_t trigger, uint64_t nowMs) {
    // GPortalPlug_v1. Walking in and out are scripts the portal makes up
    // and starts like a trigger's, in game state 8 (no bars). Not rebuilt:
    // the light effect it puts at the doorway (FUN_004825b0, effect 0x3a).
    using C = gsc::Command;
    auto number = [](double v) { return gsc::Arg{gsc::Arg::Number, v, {}}; };
    auto word = [](const char* w) { return gsc::Arg{gsc::Arg::Word, 0, w}; };
    auto line = [](C c, std::vector<gsc::Arg> args) {
        gsc::Line l;
        l.command = c;
        l.args = std::move(args);
        return l;
    };
    constexpr double kPortalSound = 0x116;  // sound.mgr 278
    const world::PortalStep step = world::portalStep(link, portalPhases_[g.instance], trigger);
    auto script = std::make_shared<gsc::Script>();
    switch (step) {
    case world::PortalStep::None:
    case world::PortalStep::WalkedOut:
        return;
    case world::PortalStep::Leave:
        // Back to play (game state 1), and the other level loads.
        portalMode_ = false;
        levelChange_ = LevelChange{link.level, link.entry};
        if (std::getenv("HARBINGER_SCRIPT_LOG"))
            std::fprintf(stderr, "%llu ms: portal %u leaves for level %u, entry %u\n", (unsigned long long)nowMs,
                         unsigned(g.instance), unsigned(link.level), unsigned(link.entry));
        return;
    case world::PortalStep::WalkIn: {
        // Sound 278, wait 1.4 s, then walk to the inner trigger, which
        // fires on arrival.
        const chk::GizmoTrigger* inner = nullptr;
        for (const auto& t : level_.map().gizmoTriggers)
            if (t.id == link.inner) inner = &t;
        if (!inner) return;
        script->lines = {line(C::Sound, {number(kPortalSound), word("NoWait")}), line(C::Wait, {number(1400)}),
                         line(C::MoveTo, {number(inner->x), number(inner->y), number(0), word("NoWait")})};
        break;
    }
    case world::PortalStep::WalkOut: {
        // Sound 278, walk out to the portal's use point (its plug's target,
        // slot 1 at plug+0xc), then play resumes (Warp 0: game state 1).
        const auto out = level_.gizmoUsePoint(g);
        script->lines = {line(C::Sound, {number(kPortalSound), word("NoWait")}),
                         line(C::MoveTo, {number(out.x), number(out.y), number(0), word("Wait")}),
                         line(C::Warp, {number(0)})};
        break;
    }
    }
    portalMode_ = true;
    if (player_.stop) player_.stop();
    runner_.start(std::move(script));
}

world::CommandResult Cutscene::moveCmd(const gsc::Line& line, bool first, uint64_t nowMs) {
    // FUN_004227c0 / FUN_004228d0 on the agent the instance names
    // (FUN_004a50e0); the player is instance 0 (likely). The first call
    // starts the action; the next is done at once without Wait, and with
    // Wait once the action has ended.
    (void)nowMs;
    using R = world::CommandResult;
    const uint16_t instance = uint16_t(line.number(2));
    const bool turn = line.command == gsc::Command::TurnToward;
    if (instance != 0) {
        if (!agents_.order) return R::Skip;
        if (first) {
            if (!agents_.order(instance, turn, line.number(0), line.number(1))) return R::Done;  // gone or dead
            return line.waits() ? R::Running : R::Done;
        }
        return agents_.busy(instance) ? R::Running : R::Done;
    }
    if (turn || !player_.walkTo) return R::Skip;
    if (first) {
        player_.walkTo(line.number(0), line.number(1));
        return line.waits() ? R::Running : R::Done;
    }
    return player_.walking() ? R::Running : R::Done;
}

void Cutscene::update(uint64_t nowMs, double x0, double y0, double x1, double y1, const Occupied& occupied) {
    nowMs_ = nowMs;
    occupied_ = &occupied;
    playerX_ = x1, playerY_ = y1;
    if (!detached_) camX_ = x1, camY_ = y1;
    for (const chk::EventTrigger* t : triggers_.step(x0, y0, x1, y1)) fire(*t, nowMs);
    const auto idle = [&](uint16_t instance) {
        const chk::GizmoRecord* g = level_.gizmoByInstance(instance);
        return !g || level_.gizmoState(*g).state == 0;
    };
    for (const chk::GizmoTrigger* t : gizmoTriggers_.step(x0, y0, x1, y1, idle)) fire(*t, nowMs);
    updateMode(nowMs);
    runner_.update([&](const gsc::Line& l, bool first, uint64_t now) { return run(l, first, now); }, nowMs);
    shakeX_ = shakeY_ = 0;
    if (nowMs < shakeUntil_)
        shakeX_ = shakeOffset(shakeModeX_, shakeAmpX_), shakeY_ = shakeOffset(shakeModeY_, shakeAmpY_);
    occupied_ = nullptr;
}

std::optional<world::Level::MapPoint> Cutscene::camera() const {
    if (!detached_) return std::nullopt;
    return world::Level::MapPoint{camX_, camY_};
}

void Cutscene::updateMode(uint64_t nowMs) {
    // The bars slide in (states 3, 4) and out (state 6), then the game goes
    // on in state 5 or 1.
    if (mode_ == Mode::In && nowMs >= modeStartMs_ + kBarMs) mode_ = Mode::On;
    if (mode_ == Mode::Out && nowMs >= modeStartMs_ + kBarMs) mode_ = Mode::Off;
}

float Cutscene::barHeight() const {
    const double t = std::min(1.0, double(nowMs_ - modeStartMs_) / double(kBarMs));
    switch (mode_) {
        case Mode::Off: return 0;
        case Mode::In: return float(kBarHeight * t);
        case Mode::On: return float(kBarHeight);
        case Mode::Out: return float(kBarHeight * (1 - t));
    }
    return 0;
}

world::CommandResult Cutscene::run(const gsc::Line& line, bool first, uint64_t nowMs) {
    using R = world::CommandResult;
    if (first && std::getenv("HARBINGER_SCRIPT_LOG"))
        std::fprintf(stderr, "%llu ms: line %d, %s\n", (unsigned long long)nowMs, line.lineNumber, gsc::name(line.command));
    using C = gsc::Command;
    switch (line.command) {
        case C::Cinematic: return cinematicCmd(line, first, nowMs);
        case C::PositionCamera: return cameraCmd(line, first, nowMs);
        case C::ThoughtBubble: return bubbleCmd(line, first, nowMs);
        case C::Sound: return soundCmd(line, first, nowMs);
        case C::ActivateGizmo: return gizmoCmd(line, first, nowMs);
        case C::MoveTo:
        case C::TurnToward: return moveCmd(line, first, nowMs);
        case C::Warp:
            // GGame_v0 case 0x11 -> FUN_004a49e0: 0 puts the game back in
            // play (state 1).
            if (line.number(0) == 0) portalMode_ = false;
            return R::Done;
        case C::Wait:
            // GGame_v0 case 0xc.
            if (first) waitUntil_ = nowMs + uint64_t(line.number(0));
            return nowMs >= waitUntil_ ? R::Done : R::Running;
        case C::Shake:
            // GGame_v0 case 0xd: amplitudes, duration, a mode for each axis;
            // the script waits out the shake.
            if (first) {
                shakeAmpX_ = int(line.number(0)), shakeAmpY_ = int(line.number(1));
                shakeUntil_ = nowMs + uint64_t(line.number(2));
                shakeModeX_ = shakeMode(line.text(3)), shakeModeY_ = shakeMode(line.text(4));
            }
            return nowMs >= shakeUntil_ ? R::Done : R::Running;
        case C::Highlight:
            // FUN_004a3270: the gizmo (or agent) to light up, or none.
            highlightOn_ = line.text(2) == "On";
            if (line.text(0) == "Gizmo") highlight_ = uint16_t(line.number(1));
            return R::Done;
        default:
            // The other agent commands (Attack, StrikePose) are not rebuilt
            // yet: the trigger scripts the levels use have none.
            return R::Skip;
    }
}

world::CommandResult Cutscene::cinematicCmd(const gsc::Line& line, bool first, uint64_t nowMs) {
    // FUN_004a4aa0: On starts the bars from the playing state and is done
    // once they are in; Off starts them out and is done once play resumes.
    (void)first;
    using R = world::CommandResult;
    if (line.text(0) == "On") {
        if (mode_ == Mode::Off) {
            mode_ = Mode::In, modeStartMs_ = nowMs;
            return R::Running;
        }
        return mode_ == Mode::On ? R::Done : R::Running;
    }
    if (mode_ == Mode::On) {
        mode_ = Mode::Out, modeStartMs_ = nowMs;
        return R::Running;
    }
    return mode_ == Mode::Off ? R::Done : R::Running;
}

world::CommandResult Cutscene::cameraCmd(const gsc::Line& line, bool first, uint64_t nowMs) {
    // FUN_004a4c10. A negative x means onto the player (where the player
    // stands now). The camera slides at `speed` cells a second, and the
    // script waits for it whether the line says Wait or not; speed 0 jumps.
    using R = world::CommandResult;
    if (first) {
        ontoPlayer_ = line.number(0) < 0;
        toX_ = ontoPlayer_ ? playerX_ : line.number(0);
        toY_ = ontoPlayer_ ? playerY_ : line.number(1);
        const double speed = line.number(2);
        if (speed != 0) {
            fromX_ = camX_, fromY_ = camY_;
            moveStartMs_ = nowMs;
            moveMs_ = uint64_t(std::hypot(fromX_ - toX_, fromY_ - toY_) / speed * 1000.0);
            return R::Running;
        }
    } else if (nowMs < moveStartMs_ + moveMs_) {
        const double t = double(nowMs - moveStartMs_) / double(moveMs_);
        camX_ = fromX_ + (toX_ - fromX_) * t, camY_ = fromY_ + (toY_ - fromY_) * t;
        detached_ = true;
        return R::Running;
    }
    camX_ = toX_, camY_ = toY_;
    detached_ = !ontoPlayer_;
    return R::Done;
}

world::CommandResult Cutscene::bubbleCmd(const gsc::Line& line, bool first, uint64_t nowMs) {
    // FUN_00422ba0. The bubble shows with its voice line; the command runs
    // while the line plays. Then without Wait the bubble closes, and with
    // Wait (or with no voice line) it stays until the player closes it.
    using R = world::CommandResult;
    const uint16_t voice = line.args.size() > 5 && line.args[5].kind == gsc::Arg::Number ? uint16_t(line.number(5)) : 0;
    if (first) {
        showBubble(line.text(0), int(line.number(1)), int(line.number(2)), int(line.number(3)), int(line.number(4)),
                   voice, nowMs);
        return R::Running;
    }
    if (bubble_.shown && bubble_.voice == voice) {
        if (!bubble_.voicing) return R::Running;
        if (playing(kVoiceKey, nowMs)) return R::Running;
        stopPcm(kVoiceKey);
        bubble_.voicing = false;
        if (line.waits()) return R::Running;
        closeBubble();
        return R::Done;
    }
    if (bubble_.voicing) stopPcm(kVoiceKey), bubble_.voicing = false;
    return R::Done;
}

world::CommandResult Cutscene::soundCmd(const gsc::Line& line, bool first, uint64_t nowMs) {
    // FUN_004a4e80: a sound.mgr sound; with Wait the script waits for it.
    using R = world::CommandResult;
    const uint16_t id = uint16_t(line.number(0));
    const int key = kSoundKey + id;
    if (first) {
        const audio::Pcm* pcm = id ? assets_.sound(id) : nullptr;
        if (!pcm || !playPcm(key, *pcm, nowMs)) return R::Done;
        return R::Running;
    }
    if (line.waits() && playing(key, nowMs)) return R::Running;
    return R::Done;
}

world::CommandResult Cutscene::gizmoCmd(const gsc::Line& line, bool first, uint64_t nowMs) {
    // FUN_004b9750: GIActivatable slot 1; with Wait the script waits until
    // the gizmo reaches state 3.
    using R = world::CommandResult;
    const chk::GizmoRecord* g = level_.gizmoByInstance(uint16_t(line.number(0)));
    if (!g) return R::Skip;
    if (first) {
        const bool used = level_.useGizmo(*g, nowMs, *occupied_);
        // A gizmo that does not take the activation does not hold the script up.
        return used && line.waits() ? R::Running : R::Done;
    }
    return level_.gizmoState(*g).state == 3 ? R::Done : R::Running;
}

bool Cutscene::playPcm(int key, const audio::Pcm& pcm, uint64_t nowMs) {
    if (sound_ && sound_->enabled()) sound_->play(key, pcm);
    soundEnds_[key] = nowMs + pcmMs(pcm);
    return true;
}

bool Cutscene::playing(int key, uint64_t nowMs) const {
    if (sound_ && sound_->enabled()) return sound_->playing(key);
    const auto it = soundEnds_.find(key);
    return it != soundEnds_.end() && nowMs < it->second;
}

void Cutscene::stopPcm(int key) {
    soundEnds_.erase(key);
    if (sound_) sound_->stop(key);
}

void Cutscene::showBubble(const std::string& text, int x, int y, int w, int h, uint16_t voice, uint64_t nowMs) {
    // FUN_0049f8e0: with no size given, the text measured in font 7 at most
    // 256 wide, plus 22 x 18 (the 1-pixel frame and the text box padding),
    // at most 256 high. A bubble already up is replaced.
    const fnt::Font& font = assets_.font(kBubbleFont);
    if (w == 0 || h == 0) {
        const auto lines = ui::wrapText(font, text, 256);
        int tw = 0;
        for (const auto& l : lines) tw = std::max(tw, ui::measure(font, l).width);
        w = tw + 2 * kBubblePadX + 2;
        h = std::min(256, int(lines.size()) * font.lineHeight + 2 * kBubblePadY + 2);
    }
    // FUN_004c97d0: positions are for 800 x 600; at 1024 x 768 the bubble's
    // centre moves out by 0.28 of itself. The rebuild scales the same way
    // for any view size.
    x += int((screenW_ / 800.0 - 1) * (x + w / 2));
    y += int((screenH_ / 600.0 - 1) * (y + h / 2));

    if (bubble_.voicing) stopPcm(kVoiceKey);
    SDL_DestroyTexture(bubble_.face.tex);
    bubble_ = {};
    bubble_.shown = true;
    bubble_.text = text;
    bubble_.voice = voice;
    bubble_.rect = {float(x), float(y), float(w), float(h)};

    // The window is the background; the text box sits 1 pixel in on the
    // green Screen texture, cut to the box (the window clips it).
    const std::vector<gim::Color>& palette = assets_.image(kBubbleBackground).palette;
    render::RgbaImage face = assets_.rgba(kBubbleBackground, &palette);
    render::RgbaImage window;
    window.width = w, window.height = h;
    window.pixels.assign(size_t(w) * size_t(h), render::packRgba(0, 0, 0, 255));
    for (int v = 0; v < h && v < face.height; ++v)
        for (int u = 0; u < w && u < face.width; ++u)
            window.pixels[size_t(v) * size_t(w) + size_t(u)] = face.pixels[size_t(v) * size_t(face.width) + size_t(u)];
    const render::RgbaImage screen = assets_.rgba(kBubbleScreen, &palette);
    render::RgbaImage back;
    back.width = std::max(0, w - 2), back.height = std::max(0, h - 2);
    back.pixels.resize(size_t(back.width) * size_t(back.height));
    for (int v = 0; v < back.height; ++v)
        for (int u = 0; u < back.width; ++u)
            back.pixels[size_t(v) * size_t(back.width) + size_t(u)] =
                screen.pixels[size_t(v % screen.height) * size_t(screen.width) + size_t(u % screen.width)];
    itf::TextBox box;
    box.width = uint16_t(back.width), box.height = uint16_t(back.height);
    box.font = box.fontAlt = kBubbleFont, box.fontStar = 11;
    box.padX = kBubblePadX, box.padY = kBubblePadY;
    const ui::BoxFonts fonts{&font, &font, &assets_.font(11)};
    const render::RgbaImage inner = ui::renderTextBox(fonts, palette, box, text, -1, &back);
    for (int v = 0; v < inner.height && v + 1 < h; ++v)
        for (int u = 0; u < inner.width && u + 1 < w; ++u) {
            const uint32_t p = inner.pixels[size_t(v) * size_t(inner.width) + size_t(u)];
            if (p >> 24) window.pixels[size_t(v + 1) * size_t(w) + size_t(u + 1)] = p;
        }
    bubble_.face = assets_.upload(window.pixels, window.width, window.height);

    if (voice)
        if (const std::string* path = speech_.find(voice)) {
            auto it = voices_.find(voice);
            if (it == voices_.end()) {
                audio::Pcm pcm;
                try {
                    audio::Mp3Stream mp3(assets_.read(*path));
                    pcm.rate = mp3.rate(), pcm.channels = mp3.channels();
                    std::vector<int16_t> chunk(size_t(4096) * size_t(std::max(1, pcm.channels)));
                    while (size_t n = mp3.read(chunk.data(), 4096))
                        pcm.samples.insert(pcm.samples.end(), chunk.begin(), chunk.begin() + std::ptrdiff_t(n) * pcm.channels);
                } catch (const std::exception& ex) {
                    SDL_Log("voice %u: %s", unsigned(voice), ex.what());
                }
                it = voices_.emplace(voice, std::move(pcm)).first;
            }
            if (!it->second.samples.empty()) bubble_.voicing = playPcm(kVoiceKey, it->second, nowMs);
        }
}

void Cutscene::closeBubble() {
    // FUN_004a5150 / FUN_0049f9f0.
    if (bubble_.voicing) stopPcm(kVoiceKey);
    SDL_DestroyTexture(bubble_.face.tex);
    bubble_ = {};
}

bool Cutscene::click(float x, float y) {
    // The bubble's Close Button (control 3, event 0x16 -> FUN_0049f9f0)
    // has no image of its own; the rebuild takes a click anywhere on the
    // bubble as pressing it.
    if (!bubble_.shown) return false;
    const SDL_FRect& r = bubble_.rect;
    if (x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h) return false;
    closeBubble();
    return true;
}

bool Cutscene::dismiss() {
    if (!bubble_.shown) return false;
    closeBubble();
    return true;
}

void Cutscene::draw() const {
    const float bar = barHeight();
    if (bar > 0) {
        SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
        const SDL_FRect top{0, 0, float(screenW_), bar}, bottom{0, float(screenH_) - bar, float(screenW_), bar};
        SDL_RenderFillRect(ren_, &top);
        SDL_RenderFillRect(ren_, &bottom);
    }
    if (bubble_.shown && bubble_.face.tex) SDL_RenderTexture(ren_, bubble_.face.tex, nullptr, &bubble_.rect);
}

}  // namespace harbinger::app
