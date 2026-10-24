// gim2tga: convert a .gim (all frames side by side) to a 32-bit .tga for viewing.
//   gim2tga <in.gim> <out.tga>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include "formats/gim.hpp"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <in.gim> <out.tga>\n", argv[0]);
        return 2;
    }
    try {
        std::ifstream in(argv[1], std::ios::binary);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
        const auto img = harbinger::gim::decode(data);
        const size_t n = img.frames.size(), w = img.width * (n ? n : 1), h = img.height;
        std::vector<uint8_t> out(18 + w * h * 4, 0);
        out[2] = 2;  // uncompressed true colour
        out[12] = uint8_t(w), out[13] = uint8_t(w >> 8), out[14] = uint8_t(h), out[15] = uint8_t(h >> 8);
        out[16] = 32, out[17] = 0x28;  // 8 alpha bits, top-left origin
        for (size_t f = 0; f < n; ++f)
            for (size_t y = 0; y < h; ++y)
                for (size_t x = 0; x < img.width; ++x) {
                    const size_t src = y * img.width + x;
                    if (!img.frames[f].opaque[src]) continue;
                    const uint8_t i = img.frames[f].index[src];
                    const harbinger::gim::Color c = i < img.palette.size() ? img.palette[i] : harbinger::gim::Color{i, i, i, 255};
                    uint8_t* p = &out[18 + (y * w + f * img.width + x) * 4];
                    p[0] = c.b, p[1] = c.g, p[2] = c.r, p[3] = 255;
                }
        std::ofstream(argv[2], std::ios::binary).write(reinterpret_cast<const char*>(out.data()), out.size());
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 0;
}
