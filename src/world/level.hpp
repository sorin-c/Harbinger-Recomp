// A loaded level: its map cells, the act's brushes and their decoded images,
// its gizmos (level objects) and their templates, and the isometric
// projection the original draws them with.
// Format notes and evidence: docs/formats/level.md
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/brs.hpp"
#include "formats/chk.hpp"
#include "formats/giz.hpp"
#include "render/sprite.hpp"

namespace harbinger::world {

// Screen position of a map point, in pixels, before the camera offset. Map
// cells are 128x64 diamonds: cell (x, y) has its top corner at toScreen(x, y),
// +x runs down-right and +y down-left (the vertex/UV order of FUN_004e2eb0).
struct ScreenPoint {
    double x, y;
};
inline ScreenPoint toScreen(double x, double y) { return {(x - y) * 64.0, (x + y) * 32.0}; }
constexpr int kTileW = 128, kTileH = 64;

class Level {
public:
    // Loads level `id` from level.mgr, and the brushes and gizmo templates of
    // its act ([ActN] Brushes, [ActN] Gizmos).
    Level(Vfs& vfs, const Ini& ini, uint16_t id);

    const chk::Level& map() const { return map_; }
    const std::string& path() const { return path_; }

    // The brush a layer paints with, or nullptr if the act has no such brush.
    const brs::Brush* brush(uint16_t id) const;
    // The decoded image for a layer's tile (cached), or nullptr.
    const render::RgbaImage* image(const chk::Layer& layer);

    // A centre tile's 128x64 diamond cut from its texture (transparent outside
    // the diamond), or an empty image if `layer` is not a centre tile.
    render::RgbaImage cutCentre(const chk::Layer& layer);

    // Draws one layer of cell (x, y) into `dst`, whose pixel (0, 0) is map
    // screen position (originX, originY). Centre tiles are cut from their
    // texture as a diamond; other tiles are drawn whole, bottom-aligned on the
    // cell's diamond (walls rise above it).
    void drawLayer(render::RgbaImage& dst, int x, int y, int layer, int originX, int originY);

    // The template a gizmo record places (loaded with the level).
    const giz::Template& gizmoTemplate(const chk::GizmoRecord& g) const;
    // The decoded image of a gizmo frame (cached), or nullptr if missing.
    const render::RgbaImage* gizmoImage(const giz::Frame& frame);

    // Where a gizmo's first frame is drawn: its top-left corner in map screen
    // pixels. FUN_004b3690 maps image pixel (64, height - (rows + 1) * 32) to
    // the gizmo's map position, `rows` being the first mask's row count.
    ScreenPoint gizmoTopLeft(const chk::GizmoRecord& g);
    // Draw order key: larger is nearer the viewer. Comparable with a cell's x + y.
    double gizmoDepth(const chk::GizmoRecord& g) const;
    // False for gizmos whose first frame is an editor placeholder: an
    // umbilical shell's first image is only the outline of its footprint, and
    // the original shows nothing there until the shell closes.
    bool gizmoVisible(const chk::GizmoRecord& g) const;
    // Draws a visible gizmo's first frame into `dst` (see drawLayer for the origin).
    void drawGizmo(render::RgbaImage& dst, const chk::GizmoRecord& g, int originX, int originY);

private:
    struct LoadedBrush {
        brs::Brush brush;
    };

    Vfs& vfs_;
    std::string path_;
    chk::Level map_;
    std::map<uint16_t, LoadedBrush> brushes_;
    std::map<uint16_t, giz::Template> gizmos_;
    std::map<std::string, std::unique_ptr<render::RgbaImage>> images_;  // by tile path
    std::map<std::string, std::unique_ptr<render::RgbaImage>> gizmoImages_;  // by frame path
};

}  // namespace harbinger::world
