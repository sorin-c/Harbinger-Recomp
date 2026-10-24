// A loaded level: its map cells, the act's brushes and their decoded images,
// its gizmos (level objects) and their templates, and the isometric
// projection the original draws them with.
// Format notes and evidence: docs/formats/level.md
#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/agt.hpp"
#include "formats/anm.hpp"
#include "formats/brs.hpp"
#include "formats/chk.hpp"
#include "formats/giz.hpp"
#include "render/sprite.hpp"
#include "world/gizmo_state.hpp"

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
    // Loads level `id` from level.mgr, the brushes and gizmo templates of its
    // act ([ActN] Brushes, [ActN] Gizmos) and the agent templates its agents
    // use ([Paths] Agents).
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
    // A gizmo's 3D shape as the camera sees it (giz::MeshPiece). The original
    // draws each gizmo as triangles in map space with a depth buffer; the
    // camera looks along (-1, -1, -1), so at any one pixel comparing two
    // surfaces' map x + y orders them as the depth buffer would. `depth`
    // holds, for each pixel of the first frame's image, the x + y of the
    // nearest point of the shape there, relative to the gizmo's x + y, or
    // NaN where the shape does not cover the pixel.
    struct DepthMap {
        int width = 0, height = 0;
        std::vector<float> depth;
        float at(int x, int y) const {
            return x < 0 || y < 0 || x >= width || y >= height ? NAN : depth[size_t(y) * size_t(width) + size_t(x)];
        }
    };
    const DepthMap& gizmoShape(const chk::GizmoRecord& g);
    // +1 if gizmo `a` hides `b` where their shapes overlap on screen, -1 if
    // `b` hides `a`, 0 if they do not overlap: at each pixel both shapes
    // cover, the larger map x + y is nearer; most such pixels decide.
    int gizmoInFront(const chk::GizmoRecord& a, const chk::GizmoRecord& b);
    // Frame `i` of a gizmo as the original shows it: only the pixels its
    // shape covers (the original draws nothing but the shape's triangles, so
    // the editor outlines some first frames carry never show). Cached.
    const render::RgbaImage* gizmoFrameImage(const chk::GizmoRecord& g, size_t i);
    // False for gizmos the base variant drops (chk::Level::keeps), and for
    // gizmos whose first frame is an editor placeholder: an
    // umbilical shell's first image is only the outline of its footprint, and
    // the original shows nothing there until the shell closes.
    bool gizmoVisible(const chk::GizmoRecord& g) const;
    // A gizmo's state now and how it plays in it (doors open or shut,
    // consoles idling or without power; world/gizmo_state.hpp). The level
    // starts each gizmo in the state its record gives.
    const GizmoState& gizmoState(const chk::GizmoRecord& g) const { return gizmoRuntime_.at(g.instance).state; }
    const GizmoPlay& gizmoPlay(const chk::GizmoRecord& g) const { return gizmoRuntime_.at(g.instance).play; }
    // The frame a gizmo shows `ms` after the level starts (for a gizmo whose
    // state changed, the time counts from the change).
    size_t gizmoFrame(const chk::GizmoRecord& g, uint64_t ms) const;
    // The mask (walk footprint) of the frame a gizmo shows at the last update.
    size_t gizmoMask(const chk::GizmoRecord& g) const;
    // The gizmo record with this instance number, or nullptr.
    const chk::GizmoRecord* gizmoByInstance(uint16_t instance) const;

    // Where an agent goes to use a gizmo: the template's offset point
    // (gizmo+0x34/+0x36, a pixel of the first frame) turned into a map
    // position by FUN_004b3690, as GFakeGizmoPlug_v22 places the plug's
    // target; the middle of the gizmo's cell when the template has none.
    struct MapPoint {
        double x, y;
    };
    MapPoint gizmoUsePoint(const chk::GizmoRecord& g);
    // Uses a gizmo at `nowMs` (GIActivatable slot 1): a console with power
    // starts its activation and toggles the gizmos attached to it (the
    // level's attachments list; a door does not shut on an agent, which
    // `occupied` reports), a chest opens. Returns false if nothing happened.
    bool useGizmo(const chk::GizmoRecord& g, uint64_t nowMs,
                  const std::function<bool(const chk::GizmoRecord&)>& occupied);
    // Moves time on to `nowMs`: gizmos whose frames have played settle in
    // their next state, and consoles return to idle once what they toggled
    // has settled. Returns the gizmos whose walk footprint (mask) changed.
    std::vector<const chk::GizmoRecord*> update(uint64_t nowMs);
    // Where frame `i` is drawn: at its (x, y) on the first frame's image.
    ScreenPoint gizmoFrameTopLeft(const chk::GizmoRecord& g, size_t i);
    // The frames to draw, in order, to show frame `f`. The original keeps
    // the gizmo's picture and draws each new frame over it at its (x, y)
    // (GAnimatedGizmo_v12), so a picture builds up from the last frame
    // that covers it all: one at (0, 0) as big as the first frame
    // (GAnimatedGizmo_v24 drops its colour key) or the first frame itself.
    std::vector<size_t> gizmoLayers(const chk::GizmoRecord& g, size_t f);
    // Draws a visible gizmo into `dst` (see drawLayer for the origin) as it
    // looks `ms` after the level starts.
    void drawGizmo(render::RgbaImage& dst, const chk::GizmoRecord& g, int originX, int originY, uint64_t ms = 0);

    // An agent's picture at a moment: the frame of its idle animation in the
    // direction it faces, and the point in it that sits on the agent's map
    // position. `key` names the frame (for texture caches).
    struct Pose {
        std::string key;
        const render::RgbaImage* image = nullptr;
        int anchorX = 0, anchorY = 0;
    };
    const agt::Template& agentTemplate(const chk::AgentRecord& a) const;
    bool agentVisible(const chk::AgentRecord& a) const { return map_.keeps(a); }
    // `ms` is the time since the animation started. Frames advance every
    // kAgentFrameMs (FUN_0042c020 divides the elapsed time by agent+0x1fa,
    // 100 unless a template sets an attack speed) and loop.
    Pose agentPose(const chk::AgentRecord& a, uint64_t ms = 0);
    double agentDepth(const chk::AgentRecord& a) const { return a.x + a.y; }
    void drawAgent(render::RgbaImage& dst, const chk::AgentRecord& a, int originX, int originY, uint64_t ms = 0);
    static constexpr uint64_t kAgentFrameMs = 100;

private:
    struct LoadedBrush {
        brs::Brush brush;
    };

    Vfs& vfs_;
    std::string path_;
    chk::Level map_;
    std::map<uint16_t, LoadedBrush> brushes_;
    std::map<uint16_t, giz::Template> gizmos_;
    std::map<uint16_t, agt::Template> agents_;
    // An agent's idle animation in one direction, ready to draw.
    struct AgentAnim {
        std::string sheet;
        size_t frames = 0;   // FUN_0042c020 loops over the first direction's count
        int anchorX = 0, anchorY = 0;
        const std::vector<render::RgbaImage>* images = nullptr;
    };
    const AgentAnim& agentAnim(const chk::AgentRecord& a);
    std::map<std::tuple<uint16_t, uint8_t, size_t>, AgentAnim> agentAnims_;  // by agent id, facing, variant
    std::map<std::string, std::vector<render::RgbaImage>> agentSheets_;  // every frame, by sheet path
    std::map<std::string, std::unique_ptr<render::RgbaImage>> images_;  // by tile path
    std::map<std::string, std::unique_ptr<render::RgbaImage>> gizmoImages_;  // by frame path
    std::map<const giz::Template*, DepthMap> gizmoShapes_;
    std::map<std::pair<const giz::Template*, size_t>, std::unique_ptr<render::RgbaImage>> gizmoFrames_;
    struct GizmoRuntime {
        GizmoState state;
        GizmoPlay play;
        uint64_t startMs = 0;  // when the state began
        uint64_t endMs = 0;    // when a Once play ends (0: it does not)
        std::vector<uint16_t> waitingOn;  // a console: the attached gizmos still moving
        size_t mask = 0;       // the mask shown at the last update
        bool maskVaries = false;  // its frames do not all use one mask
    };
    void setGizmoState(const chk::GizmoRecord& g, GizmoRuntime& rt, uint64_t nowMs);
    std::map<uint16_t, GizmoRuntime> gizmoRuntime_;  // by instance
    std::map<uint16_t, size_t> gizmoIndex_;           // instance -> index in map_.gizmos
    uint64_t nowMs_ = 0;
};

}  // namespace harbinger::world
