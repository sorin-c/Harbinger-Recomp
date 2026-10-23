// Tests for the VFS and sprite conversion. Real-data cases need HARBINGER_DATA.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "engine/vfs.hpp"
#include "formats/gim.hpp"
#include "render/sprite.hpp"

using namespace harbinger;

static int failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

static gim::Image keyedImage(uint8_t encoding) {
    gim::Image img;
    img.width = 3;
    img.height = 1;
    img.flags = uint8_t(0x30 | encoding);
    img.palette = {{0, 255, 0, 255}, {255, 0, 0, 255}, {10, 20, 30, 128}};
    img.frames.push_back({{0, 2, 1}, {1, 1, 1}});
    return img;
}

static void testSprite() {
    auto img = keyedImage(5);
    auto px = render::toRgba(img, 0, render::KeyMode::Shadow);
    CHECK(px[0] >> 24 == 96 && (px[0] & 0xFFFFFF) == 0);              // key -> translucent black
    CHECK(px[1] == (10u | 20u << 8 | 30u << 16 | 255u << 24));      // opaque palette colour
    CHECK(px[2] == 0);                                               // red rim hidden
    CHECK(render::toRgba(img, 0, render::KeyMode::Highlight)[2] >> 24 == 255);
    CHECK(render::toRgba(img, 0, render::KeyMode::Transparent)[0] == 0);
    CHECK((render::toRgba(img, 0, render::KeyMode::Raw)[0] & 0xFFFFFF) == (255u << 8));

    img.frames[0].opaque = {1, 1, 0};
    CHECK(render::toRgba(img, 0)[2] == 0);  // RLE-transparent pixel

    auto alpha = keyedImage(7);
    CHECK(render::toRgba(alpha, 0)[1] >> 24 == 128);  // encoding 7 keeps palette alpha

    auto raw = keyedImage(0);  // raw images: markers are normal colours
    CHECK(render::toRgba(raw, 0)[0] == (255u << 8 | 255u << 24));
}

static void testVfs(const char* dir) {
    Vfs vfs(dir);
    CHECK(vfs.archiveCount() == 2);
    // A file only res1 has, and one where res1 overrides a different res0 copy.
    CHECK(vfs.exists("res/common/dialog/culibine/(con)hatchery.dlg"));
    auto over = vfs.read("Res\\Act1\\Levels\\(g)airlock.chk");
    sqh::Archive res1(std::string(dir) + "/res1.sqh");
    const auto* e = res1.find("res/act1/levels/(g)airlock.chk");
    CHECK(over && e && *over == res1.read(*e));
    // The ini names images as .tga; they are stored as .gim.
    auto sight = vfs.read("res\\common\\images\\misc\\sight.tga");
    CHECK(sight.has_value());
    if (sight) CHECK(gim::decode(*sight).frames.size() == 1);
    // Loose file fallback.
    auto ini = vfs.read("Harbinger.ini");
    CHECK(ini && ini->size() > 100 && std::memcmp(ini->data(), "[Paths]", 7) == 0);
    CHECK(!vfs.read("res/no/such/file.gim"));
    CHECK(vfs.list("res/act1/managers/").size() >= 2);
}

int main() {
    testSprite();
    if (const char* dir = std::getenv("HARBINGER_DATA"))
        testVfs(dir);
    else
        std::printf("HARBINGER_DATA not set; skipping real-data checks\n");
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
