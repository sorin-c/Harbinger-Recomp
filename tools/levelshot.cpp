// levelshot: draw a level's floor and walls to a .tga, for checking the level
// and brush formats against the original.
//   levelshot <install dir> <level id> <out.tga> [layers=2]
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "world/level.hpp"

using namespace harbinger;

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <install dir> <level id> <out.tga> [layers]\n", argv[0]);
        return 2;
    }
    try {
        Vfs vfs(argv[1]);
        const auto iniData = vfs.read("Harbinger.ini");
        if (!iniData) throw std::runtime_error("Harbinger.ini not found");
        const Ini ini = Ini::fromBytes(*iniData);
        world::Level level(vfs, ini, uint16_t(std::atoi(argv[2])));
        const int layers = argc > 4 ? std::atoi(argv[4]) : 2;
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

        // Floors first, then the upper layer back to front.
        for (int x = 0; x < m.width; ++x)
            for (int y = 0; y < m.height; ++y) level.drawLayer(img, x, y, 0, originX, originY);
        if (layers > 1)
            for (int d = 0; d < m.width + m.height; ++d)
                for (int x = 0; x < m.width; ++x)
                    if (int y = d - x; y >= 0 && y < m.height) level.drawLayer(img, x, y, 1, originX, originY);

        for (const auto& e : m.entries) {
            const auto p = world::toScreen(e.x, e.y);
            std::printf("entry %u \"%s\" at (%.2f, %.2f) -> pixel (%d, %d)\n", e.id, e.name.c_str(), e.x, e.y,
                        int(p.x) - originX, int(p.y) - originY);
        }
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
