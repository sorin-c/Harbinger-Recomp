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
    // Click to move: walk to the map point under view pixel (x, y).
    void walkTo(float x, float y);
    // Click to attack: if an enemy is drawn under view pixel (x, y), go and
    // hit it and return true.
    bool attackAt(float x, float y);
    // The pad's attack button: hit the nearest enemy within a few cells,
    // preferring those ahead. False if there is none.
    bool attackNearest();
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

    Assets& assets_;
    SDL_Renderer* ren_;
    std::unique_ptr<world::Level> level_;
    std::unique_ptr<world::WalkMap> walk_;
    std::unique_ptr<world::Player> player_;
    world::Rng rng_;
    std::unique_ptr<world::Foes> foes_;
    int screenW_, screenH_;
    double camX_ = 0, camY_ = 0;  // map screen position at the centre of the view
    uint64_t startMs_ = 0, elapsedMs_ = 0;  // animation clock, from the first update
    // GPU copies of tile images, by brush, tile id (centre tiles are cut per id).
    mutable std::map<std::pair<uint16_t, uint16_t>, Texture> tiles_;
    // Visible gizmos and agents, back to front, with their depth keys.
    struct Object {
        double depth;
        const chk::GizmoRecord* gizmo;
        const chk::AgentRecord* agent;
        double minX = 0, minY = 0, maxX = 0, maxY = 0;  // a gizmo's footprint in map cells
    };
    // Where the player goes in the back-to-front order (see draw()).
    double playerDepth() const;
    std::vector<Object> objects_;
    mutable std::map<std::string, Texture> objectTextures_;  // by image path
};

}  // namespace harbinger::app
