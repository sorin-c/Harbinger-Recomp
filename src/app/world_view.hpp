// The in-game view, as far as it is rebuilt: a level's floor, walls, gizmos
// and characters, drawn and animated from the original files, and the
// player's character walking on it with the camera following, and fighting
// the level's enemies hand to hand.
#pragma once

#include <SDL3/SDL.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/assets.hpp"
#include "world/combat.hpp"
#include "world/foes.hpp"
#include "world/level.hpp"
#include "world/player.hpp"
#include "world/walkmap.hpp"

namespace harbinger::app {

class WorldView {
public:
    // Loads level `id`, stands the player's character on its "Default"
    // entry point and centres the camera on it.
    WorldView(Assets& assets, SDL_Renderer* ren, uint16_t levelId, world::Character character, int screenW,
              int screenH);
    ~WorldView();
    WorldView(const WorldView&) = delete;
    WorldView& operator=(const WorldView&) = delete;

    // Direct control from a stick or keys: walk towards screen direction
    // (dx, dy) (any length; 0, 0 stops).
    void steer(float dx, float dy);
    // Click to move: walk to the map point under view pixel (x, y). Drops a
    // gizmo the player was on the way to use.
    void walkTo(float x, float y);
    // Click to attack: if an enemy is drawn under view pixel (x, y), go and
    // hit it and return true.
    bool attackAt(float x, float y);
    // The pad's attack button: hit the nearest enemy within a few cells,
    // preferring those ahead. False if there is none.
    bool attackNearest();
    // Using gizmos (consoles, chests): walk up to the usable gizmo under view
    // pixel (x, y) and use it on arrival; false if there is none there.
    bool useAt(float x, float y);
    // The same for the nearest usable gizmo within kUseRange cells of the
    // player (pad and keys).
    bool useNearest();
    bool usePending() const { return pendingUse_ != nullptr; }
    // Rebuild choices: how near a gizmo's use point the player must stand
    // to use it, and how far the use button looks for one, in map cells.
    static constexpr double kUseReach = 1.5, kUseRange = 3.0;
    // The map point under view pixel (x, y).
    world::WalkMap::Point mapPoint(float x, float y) const;
    void update(uint64_t nowMs);
    void draw() const;

    const world::Level& level() const { return *level_; }
    const world::Player& player() const { return *player_; }
    const world::Foes& foes() const { return *foes_; }

private:
    const Texture* tile(const chk::Layer& layer) const;
    void drawLayer(int x, int y, int layer) const;
    void drawGizmo(const chk::GizmoRecord& g) const;
    void drawAgent(const chk::AgentRecord& a) const;
    void drawPlayer() const;
    void drawTargetBar() const;
    void drawImage(const std::string& key, const render::RgbaImage* img, double left, double top) const;
    const chk::GizmoRecord* gizmoAt(float x, float y) const;
    void approach(const chk::GizmoRecord& g);
    bool occupied(const chk::GizmoRecord& g) const;

    Assets& assets_;
    SDL_Renderer* ren_;
    std::unique_ptr<world::Level> level_;
    std::unique_ptr<world::WalkMap> walk_;
    std::unique_ptr<world::Player> player_;
    world::Rng rng_;
    std::unique_ptr<world::Foes> foes_;
    const chk::GizmoRecord* pendingUse_ = nullptr;  // walking up to use it
    int screenW_, screenH_;
    double camX_ = 0, camY_ = 0;  // map screen position at the centre of the view
    uint64_t startMs_ = 0, elapsedMs_ = 0;  // animation clock, from the first update
    // GPU copies of tile images, by brush, tile id (centre tiles are cut per id).
    mutable std::map<std::pair<uint16_t, uint16_t>, Texture> tiles_;
    // Visible gizmos and agents. The original draws them with a depth
    // buffer; the rebuild orders whole pictures by comparing depths where
    // they overlap (see draw()).
    struct Object {
        double depth;  // the order key against walls, and between pictures that do not overlap
        const chk::GizmoRecord* gizmo;
        const chk::AgentRecord* agent;
        const world::Level::DepthMap* shape = nullptr;  // a gizmo's
        int left = 0, top = 0;  // where a gizmo's shape sits, map screen pixels
    };
    std::vector<Object> objects_;
    std::vector<std::vector<size_t>> behind_;  // per gizmo: the gizmos it hides (they draw first)
    // A picture placed on the map: an agent's pose, or a whole gizmo.
    struct Sprite {
        const render::RgbaImage* image = nullptr;
        int left = 0, top = 0;
    };
    Sprite agentSprite(const chk::AgentRecord* a) const;  // nullptr: the player
    // +1 if gizmo object `o` is in front of a sprite standing at map depth
    // `depth` where they overlap, -1 if behind, 0 if they do not overlap.
    int compare(const Object& o, const Sprite& s, double depth) const;
    mutable std::vector<size_t> order_;  // the last draw order (objects_ indices; objects_.size() = the player)
    mutable std::map<std::string, Texture> objectTextures_;  // by image path
};

}  // namespace harbinger::app
