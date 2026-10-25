// Trigger scripts in the game view: the level's event triggers, and the
// cutscenes they start (cinematic bars, camera moves, thought bubbles with
// voice lines, screen shakes, sounds, gizmos switched on). Notes and
// evidence: docs/formats/script.md, "Running a script".
#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "app/assets.hpp"
#include "app/sound.hpp"
#include "formats/mgr.hpp"
#include "world/level.hpp"
#include "world/player.hpp"
#include "world/portal.hpp"
#include "world/script.hpp"

namespace harbinger::app {

class Cutscene {
public:
    Cutscene(Assets& assets, SDL_Renderer* ren, world::Character character, world::Level& level, int screenW,
             int screenH);
    ~Cutscene();
    Cutscene(const Cutscene&) = delete;
    Cutscene& operator=(const Cutscene&) = delete;

    void setSound(SoundSystem* sound) { sound_ = sound; }

    // How scripts move the player (agent 0 in MoveTo): walk to a map point,
    // stop, and whether it is still walking.
    struct PlayerControl {
        std::function<void(double x, double y)> walkTo;
        std::function<void()> stop;
        std::function<bool()> walking;
    };
    void setPlayer(PlayerControl control) { player_ = std::move(control); }
    // The other agents a script moves (MoveTo, TurnToward): `order` starts
    // a move (or with `turn` a turn) towards (x, y) and says whether the
    // agent is there to take it; `busy` whether it is still carrying it out.
    struct AgentControl {
        std::function<bool(uint16_t instance, bool turn, double x, double y)> order;
        std::function<bool(uint16_t instance)> busy;
    };
    void setAgents(AgentControl control) { agents_ = std::move(control); }

    // Gizmo triggers on gizmos the cutscene does not run itself (the
    // umbilical pads): the hook gets the gizmo and the trigger's id.
    using GizmoHook = std::function<void(const chk::GizmoRecord&, uint16_t trigger, uint64_t nowMs)>;
    void setGizmoHook(GizmoHook hook) { gizmoHook_ = std::move(hook); }
    // Told the id of each event trigger that fires (the story's Trigger
    // Fired event).
    void setTriggerFired(std::function<void(uint16_t trigger)> fn) { triggerFired_ = std::move(fn); }
    // GLaunchScriptEntityAction (FUN_004a45c0): starts script `id` as a
    // trigger would, replacing any that is running. False if it does not load.
    bool launch(uint16_t id);

    using Occupied = std::function<bool(const chk::GizmoRecord&)>;
    // Once a frame, after the player has moved from (x0, y0) to (x1, y1):
    // fires the triggers that move sets off and runs the current script.
    void update(uint64_t nowMs, double x0, double y0, double x1, double y1, const Occupied& occupied);

    // Cinematic mode (game states 3 to 6), or a portal walking the player
    // (game state 8): the player takes no orders.
    bool cinematic() const { return mode_ != Mode::Off || portalMode_; }
    bool portalMode() const { return portalMode_; }
    // Set once the player has walked into a portal, or a script's SetLevel
    // has run: the level and entry point to load (GGame+0x132, +0x136,
    // then FUN_004a1d20). SetLevel names no entry point (kNoEntry).
    struct LevelChange {
        uint16_t level = 0, entry = 0;
        bool umbilical = false;  // picked from an umbilical's list
    };
    static constexpr uint16_t kNoEntry = 0xffff;
    const std::optional<LevelChange>& levelChange() const { return levelChange_; }
    bool running() const { return runner_.running(); }
    // Where the camera looks, in map cells, while a script holds it; empty
    // while it follows the player.
    std::optional<world::Level::MapPoint> camera() const;
    // The shake offset this frame, in view pixels.
    int shakeX() const { return shakeX_; }
    int shakeY() const { return shakeY_; }
    // Whether a Highlight command has lit up this gizmo instance.
    bool highlighted(uint16_t instance) const { return highlightOn_ && highlight_ == instance; }
    bool bubbleShown() const { return bubble_.shown; }
    const std::string& bubbleText() const { return bubble_.text; }

    // A click at view pixel (x, y): true if it closed the thought bubble.
    bool click(float x, float y);
    // The pad's or keyboard's close (a rebuild control): true if a bubble was closed.
    bool dismiss();

    // The cinematic bars and the thought bubble, over the view.
    void draw() const;

    // Rebuild choice: the bars' height and how long they take to slide in
    // or out (FUN_004a4aa0's states 3/4 and 6).
    static constexpr int kBarHeight = 50;
    static constexpr uint64_t kBarMs = 500;

private:
    world::CommandResult run(const gsc::Line& line, bool first, uint64_t nowMs);
    world::CommandResult cinematicCmd(const gsc::Line& line, bool first, uint64_t nowMs);
    world::CommandResult cameraCmd(const gsc::Line& line, bool first, uint64_t nowMs);
    world::CommandResult bubbleCmd(const gsc::Line& line, bool first, uint64_t nowMs);
    world::CommandResult soundCmd(const gsc::Line& line, bool first, uint64_t nowMs);
    world::CommandResult gizmoCmd(const gsc::Line& line, bool first, uint64_t nowMs);
    world::CommandResult moveCmd(const gsc::Line& line, bool first, uint64_t nowMs);
    void fire(const chk::EventTrigger& t, uint64_t nowMs);
    void fire(const chk::GizmoTrigger& t, uint64_t nowMs);
    void runPortal(const chk::GizmoRecord& g, const world::PortalLink& link, uint16_t trigger, uint64_t nowMs);
    std::shared_ptr<const gsc::Script> script(uint16_t id);
    // Sounds a script plays, by key; without an audio device they "play"
    // for their length so scripts keep their timing.
    bool playPcm(int key, const audio::Pcm& pcm, uint64_t nowMs);
    bool playing(int key, uint64_t nowMs) const;
    void stopPcm(int key);
    void showBubble(const std::string& text, int x, int y, int w, int h, uint16_t voice, uint64_t nowMs);
    void closeBubble();
    void updateMode(uint64_t nowMs);
    float barHeight() const;

    Assets& assets_;
    SDL_Renderer* ren_;
    world::Level& level_;
    int screenW_, screenH_;
    SoundSystem* sound_ = nullptr;
    mgr::Index scripts_, speech_;
    std::map<uint16_t, std::shared_ptr<const gsc::Script>> loaded_;
    world::TriggerWatch triggers_;
    world::GizmoTriggerWatch gizmoTriggers_;
    PlayerControl player_;
    GizmoHook gizmoHook_;
    AgentControl agents_;
    std::function<void(uint16_t)> triggerFired_;
    std::map<uint16_t, uint8_t> portalPhases_;  // portal instance -> plug+0x100
    bool portalMode_ = false;                   // GGame+0x43c = 8
    std::optional<LevelChange> levelChange_;
    world::ScriptRunner runner_;
    const Occupied* occupied_ = nullptr;  // during update()
    std::map<int, uint64_t> soundEnds_;   // key -> when it ends (no audio device)
    std::map<uint16_t, audio::Pcm> voices_;

    // GGame+0x43c: 1 playing, 3/4 bars sliding in, 5 cinematic, 6 sliding out.
    enum class Mode { Off, In, On, Out } mode_ = Mode::Off;
    uint64_t modeStartMs_ = 0, nowMs_ = 0;

    double playerX_ = 0, playerY_ = 0;
    double camX_ = 0, camY_ = 0;  // GGame+0x160/+0x168
    bool detached_ = false;       // GGame+0x431
    double fromX_ = 0, fromY_ = 0, toX_ = 0, toY_ = 0;
    bool ontoPlayer_ = false;
    uint64_t moveStartMs_ = 0, moveMs_ = 0;

    uint64_t waitUntil_ = 0;
    int shakeAmpX_ = 0, shakeAmpY_ = 0, shakeModeX_ = 0, shakeModeY_ = 0;
    uint64_t shakeUntil_ = 0;
    int shakeX_ = 0, shakeY_ = 0;
    uint16_t highlight_ = 0;
    bool highlightOn_ = false;

    struct Bubble {
        bool shown = false;
        std::string text;
        uint16_t voice = 0;    // GGame+0x584ec
        bool voicing = false;  // its voice line has not ended (the command's +0x200)
        SDL_FRect rect{};
        Texture face;
    } bubble_;
};

}  // namespace harbinger::app
