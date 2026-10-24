// The in-game view, as far as it is rebuilt: a level's floor, walls and
// gizmos drawn from the original files, with a camera the player can move
// around.
#pragma once

#include <SDL3/SDL.h>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/assets.hpp"
#include "world/level.hpp"

namespace harbinger::app {

class WorldView {
public:
    // Loads level `id` and centres the camera on its "Default" entry point.
    WorldView(Assets& assets, SDL_Renderer* ren, uint16_t levelId, int screenW, int screenH);
    ~WorldView();
    WorldView(const WorldView&) = delete;
    WorldView& operator=(const WorldView&) = delete;

    // Camera speed in screen pixels per second along x and y, from keys,
    // the screen edges or a stick: -1..1 each.
    void setScroll(float dx, float dy) { scrollX_ = dx, scrollY_ = dy; }
    void update(uint64_t nowMs);
    void draw() const;

    const world::Level& level() const { return *level_; }

private:
    const Texture* tile(const chk::Layer& layer) const;
    void drawLayer(int x, int y, int layer) const;
    void drawGizmo(const chk::GizmoRecord& g) const;
    void drawAgent(const chk::AgentRecord& a) const;
    void drawImage(const std::string& key, const render::RgbaImage* img, double left, double top) const;

    Assets& assets_;
    SDL_Renderer* ren_;
    std::unique_ptr<world::Level> level_;
    int screenW_, screenH_;
    double camX_ = 0, camY_ = 0;  // map screen position at the centre of the view
    float scrollX_ = 0, scrollY_ = 0;
    uint64_t lastMs_ = 0;
    // GPU copies of tile images, by brush, tile id (centre tiles are cut per id).
    mutable std::map<std::pair<uint16_t, uint16_t>, Texture> tiles_;
    // Visible gizmos and agents, back to front, with their depth keys.
    struct Object {
        double depth;
        const chk::GizmoRecord* gizmo;
        const chk::AgentRecord* agent;
    };
    std::vector<Object> objects_;
    mutable std::map<std::string, Texture> objectTextures_;  // by image path
};

}  // namespace harbinger::app
