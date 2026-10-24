// Tests for the .gim decoder. The synthetic cases always run; with
// HARBINGER_DATA set, every .gim in res0/res1 must decode completely.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "formats/gim.hpp"
#include "formats/sqh.hpp"

using namespace harbinger;

static int failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

static void put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}

static std::vector<uint8_t> header(uint16_t w, uint16_t h, uint16_t pitch, uint8_t flags) {
    std::vector<uint8_t> b;
    put16(b, w);
    put16(b, h);
    put16(b, pitch);
    b.push_back(1);
    b.push_back(flags);
    put16(b, 0);
    put16(b, 0);
    put16(b, 2);  // two palette entries, stored B,G,R,A
    for (uint8_t v : {0x00, 0xFF, 0x00, 0xFF, 0x10, 0x20, 0x30, 0xFF}) b.push_back(v);
    return b;
}

static void testRaw() {
    auto b = header(3, 2, 4, 0x30);
    put16(b, 8);
    put16(b, 0);
    for (uint8_t v : {1, 0, 1, 9, 0, 1, 0, 9}) b.push_back(v);  // pitch padding = 9
    auto img = gim::decode(b);
    CHECK(img.frames.size() == 1);
    CHECK(img.frames[0].index == (std::vector<uint8_t>{1, 0, 1, 0, 1, 0}));
    CHECK(img.palette[1].r == 0x30 && img.palette[1].b == 0x10);
}

static void testRle() {
    // 4x1, two frames: "..ab" and "a..." ; plus 8 extra header bytes for encoding 5.
    auto b = header(4, 1, 4, 0x35);
    for (int i = 0; i < 8; ++i) b.push_back(0xFF);
    std::vector<uint8_t> px = {7, 0, 1, 2, 2, 1, 1};  // len 7, 1 segment: skip 2, 2 literals
    // second frame: 2 segments (skip 0, 1 literal) (skip 3, 0 literals)
    std::vector<uint8_t> f2 = {8, 0, 2, 0, 1, 1, 3, 0};
    put16(b, uint16_t(px.size() + f2.size()));
    put16(b, 0);
    b.insert(b.end(), px.begin(), px.end());
    b.insert(b.end(), f2.begin(), f2.end());
    auto img = gim::decode(b);
    CHECK(img.frames.size() == 2);
    CHECK(img.frames[0].opaque == (std::vector<uint8_t>{0, 0, 1, 1}));
    CHECK(img.frames[1].opaque == (std::vector<uint8_t>{1, 0, 0, 0}));

    auto bad = b;
    bad.push_back(0);
    bool threw = false;
    try {
        gim::decode(bad);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

static void testRealData(const char* dir) {
    namespace fs = std::filesystem;
    size_t total = 0, failed = 0, frames = 0;
    std::map<int, size_t> byFlags;
    for (const char* name : {"res0.sqh", "res1.sqh"}) {
        sqh::Archive arc((fs::path(dir) / name).string());
        for (const auto& e : arc.entries()) {
            if (e.path.size() < 4 || e.path.compare(e.path.size() - 4, 4, ".gim") != 0) continue;
            ++total;
            try {
                auto img = gim::decode(arc.read(e));
                frames += img.frames.size();
                ++byFlags[img.flags];
            } catch (const std::exception& ex) {
                if (failed++ < 5) std::fprintf(stderr, "  %s: %s\n", e.path.c_str(), ex.what());
            }
        }
    }
    std::printf("  decoded %zu/%zu .gim files, %zu frames\n", total - failed, total, frames);
    CHECK(total == 10158);
    CHECK(failed == 0);
}

int main() {
    testRaw();
    testRle();
    if (const char* dir = std::getenv("HARBINGER_DATA"))
        testRealData(dir);
    else
        std::printf("HARBINGER_DATA not set; skipping real-data checks\n");
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
