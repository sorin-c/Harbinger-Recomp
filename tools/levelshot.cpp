// levelshot: draw a level's floor, walls and gizmos to a .tga, for checking
// the level, brush and gizmo formats against the original.
//   levelshot <install dir> <level id> <out.tga> [layers=3]
// layers: 1 = floors, 2 = and walls, 3 = and gizmos and agents, 4 = and the
// walk grid (red: blocked half cells; orange, yellow: near walls, which the
// player keeps clear of; see docs/movement.md).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <limits>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "world/level.hpp"
#include "world/walkmap.hpp"

using namespace harbinger;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <install dir> <level id> <out.tga> [layers] [ms]\n", argv[0]);
        return 2;
    }
    try {
        Vfs vfs(argv[1]);
        const auto iniData = vfs.read("Harbinger.ini");
        if (!iniData) throw std::runtime_error("Harbinger.ini not found");
        const Ini ini = Ini::fromBytes(*iniData);
        world::Level level(vfs, ini, uint16_t(std::atoi(argv[2])));
        const int layers = argc > 4 ? std::atoi(argv[4]) : 3;
        const uint64_t ms = argc > 5 ? std::strtoull(argv[5], nullptr, 10) : 0;  // animation time
        const auto& m = level.map();

        // Bounds of every cell's diamond plus wall height.
        double minX = std::numeric_limits<double>::max(), maxX = -minX, minY = minX, maxY = -minX;
        for (int x : {0, int(m.width)})
            for (int y : {0, int(m.height)}) {
                const auto p = world::toScreen(x, y);
                minX = std::min(minX, p.x), maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y), maxY = std::max(maxY, p.y);
            }
        const int originX = int(minX), originY = int(minY) - 256;
        render::RgbaImage img;
        img.width = int(maxX - minX);
        img.height = int(maxY - minY) + 256;
        img.pixels.assign(size_t(img.width) * size_t(img.height), render::packRgba(0, 0, 0, 255));

        // Floors first, then walls and gizmos back to front: a diagonal of
        // walls, then the gizmos whose depth falls before the next diagonal.
        for (int x = 0; x < m.width; ++x)
            for (int y = 0; y < m.height; ++y) level.drawLayer(img, x, y, 0, originX, originY);
        // Gizmos and agents, back to front.
        struct Object {
            double depth;
            std::function<void()> draw;
        };
        std::vector<Object> objects;
        if (layers > 2) {
            for (const auto& g : m.gizmos)
                objects.push_back({level.gizmoDepth(g), [&] { level.drawGizmo(img, g, originX, originY, ms); }});
            for (const auto& a : m.agents)
                objects.push_back({level.agentDepth(a), [&] { level.drawAgent(img, a, originX, originY, ms); }});
        }
        std::stable_sort(objects.begin(), objects.end(), [](const auto& a, const auto& b) { return a.depth < b.depth; });
        size_t next = 0;
        if (layers > 1)
            for (int d = 0; d < m.width + m.height; ++d) {
                for (int x = 0; x < m.width; ++x)
                    if (int y = d - x; y >= 0 && y < m.height) level.drawLayer(img, x, y, 1, originX, originY);
                for (; next < objects.size() && objects[next].depth < d + 1; ++next) objects[next].draw();
            }
        for (; next < objects.size(); ++next) objects[next].draw();

        if (layers > 3) {
            const world::WalkMap walk(level);
            for (int sx = 0; sx < walk.width(); ++sx)
                for (int sy = 0; sy < walk.height(); ++sy) {
                    const uint8_t c = walk.at(sx, sy).clearance;
                    if (c == 0) continue;
                    const uint32_t tint = c == world::WalkMap::kBlocked ? render::packRgba(255, 0, 0, 0)
                                          : c == 2                       ? render::packRgba(255, 128, 0, 0)
                                                                         : render::packRgba(255, 255, 0, 0);
                    const auto top = world::toScreen(sx / 2.0, sy / 2.0);
                    for (int v = 0; v < 32; ++v)
                        for (int u = -32; u < 32; ++u) {
                            if (std::abs(u + 0.5) / 32 + std::abs(v + 0.5 - 16) / 16 > 1) continue;
                            const int px = int(top.x) + u - originX, py = int(top.y) + v - originY;
                            if (px < 0 || py < 0 || px >= img.width || py >= img.height) continue;
                            uint32_t& d = img.pixels[size_t(py) * size_t(img.width) + size_t(px)];
                            uint32_t o = 0xFF000000u;
                            for (int sh = 0; sh < 24; sh += 8)
                                o |= (((d >> sh) & 0xFF) / 2 + ((tint >> sh) & 0xFF) / 2) << sh;
                            d = o;
                        }
                }
        }

        for (const auto& e : m.entries) {
            const auto p = world::toScreen(e.x, e.y);
            std::printf("entry %u \"%s\" at (%.2f, %.2f) -> pixel (%d, %d)\n", e.id, e.name.c_str(), e.x, e.y,
                        int(p.x) - originX, int(p.y) - originY);
        }
        std::printf("%zu gizmos, %zu agents\n", m.gizmos.size(), m.agents.size());
        std::printf("%s: \"%s\" act %u, %ux%u cells, image %dx%d\n", level.path().c_str(), m.name.c_str(), m.act,
                    m.width, m.height, img.width, img.height);

        std::vector<uint8_t> out(18 + img.pixels.size() * 4, 0);
        out[2] = 2;
        out[12] = uint8_t(img.width), out[13] = uint8_t(img.width >> 8);
        out[14] = uint8_t(img.height), out[15] = uint8_t(img.height >> 8);
        out[16] = 32, out[17] = 0x28;
        for (size_t i = 0; i < img.pixels.size(); ++i) {
            const uint32_t p = img.pixels[i];
            out[18 + i * 4] = uint8_t(p >> 16), out[19 + i * 4] = uint8_t(p >> 8), out[20 + i * 4] = uint8_t(p);
            out[21 + i * 4] = 255;
        }
        std::ofstream(argv[3], std::ios::binary).write(reinterpret_cast<const char*>(out.data()), long(out.size()));
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 0;
}
