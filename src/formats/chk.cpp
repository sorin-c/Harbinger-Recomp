#include "formats/chk.hpp"

#include <cstring>

#include "formats/binary.hpp"

namespace harbinger::chk {

namespace {

double f64(ByteReader& r) {
    const uint64_t lo = r.u32();
    const uint64_t bits = lo | (uint64_t(r.u32()) << 32);
    double v;
    std::memcpy(&v, &bits, sizeof v);
    return v;
}

}  // namespace

Level parse(const std::vector<uint8_t>& data) {
    ByteReader r(data, "chk");
    Level lv;
    // FUN_0049e160: version, then the act (from version 1.16), then the size.
    lv.version = f64(r);
    if (lv.version < 1.23) r.fail("unsupported version");  // every shipped level is 1.23
    lv.act = r.u16();
    lv.width = r.u16();
    lv.height = r.u16();
    if (!lv.width || !lv.height) r.fail("empty map");

    // FUN_004491c0: entry points, four bytes, two bytes, the name, two bytes.
    const uint16_t entries = r.u16();
    for (uint16_t i = 0; i < entries; ++i) {
        EntryPoint e;
        e.id = r.u16();
        e.x = f64(r);
        e.y = f64(r);
        e.facing = r.u16();
        e.name = r.string();
        e.link = r.i32();
        lv.entries.push_back(std::move(e));
    }
    lv.unknown1 = r.u16();
    lv.music = r.u16();
    lv.unknown3 = r.u16();
    lv.name = r.string();
    lv.unknown4 = r.u16();

    // FUN_00448080 / FUN_004e2d60: width * height cells, each a count and that
    // many (brush, tile) pairs. A cell holds at most two layers.
    lv.cells.resize(size_t(lv.width) * lv.height);
    for (auto& c : lv.cells) {
        const uint16_t n = r.u16();
        if (n > 2) r.fail("cell with more than two layers");
        c.count = uint8_t(n);
        for (uint16_t i = 0; i < n; ++i) {
            c.layers[i].brush = r.u16();
            c.layers[i].tile = r.u16();
        }
    }
    lv.cellsEnd = r.pos();
    return lv;
}

}  // namespace harbinger::chk
