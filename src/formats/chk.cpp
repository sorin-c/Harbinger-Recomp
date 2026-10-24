#include "formats/chk.hpp"

#include <cstring>

#include "formats/binary.hpp"
#include "formats/giz.hpp"

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

void parseGizmos(Level& lv, const std::vector<uint8_t>& data,
                 const std::function<const giz::Template*(uint16_t)>& lookup) {
    ByteReader r(data, "chk");
    r.skip(lv.cellsEnd);
    // Lists of instance ids. Every shipped level has one, holding all or most
    // of the instances below.
    const uint16_t lists = r.u16();
    lv.instanceLists.assign(lists, {});
    for (auto& list : lv.instanceLists) {
        const uint16_t n = r.u16();
        for (uint16_t i = 0; i < n; ++i) list.push_back(r.u16());
    }
    // FUN_004495a0: i32 count, then per gizmo its template id and the plug's
    // chunk load.
    const int32_t count = r.i32();
    if (count < 0 || size_t(count) > r.remaining() / 8) r.fail("implausible gizmo count");
    lv.gizmos.clear();
    lv.gizmos.reserve(size_t(count));
    for (int32_t i = 0; i < count; ++i) {
        GizmoRecord g;
        g.gizmo = r.u16();
        g.instance = r.u16();
        g.x = r.u16();
        g.y = r.u16();
        if (g.x >= lv.width || g.y >= lv.height) r.fail("gizmo outside the map");
        const giz::Template* t = lookup(g.gizmo);
        if (!t) r.fail("gizmo id missing from gizmo.mgr");
        g.extra = r.bytes(t->recordExtra(data.data() + r.pos(), r.remaining()));
        lv.gizmos.push_back(std::move(g));
    }
    lv.gizmosEnd = r.pos();
}

}  // namespace harbinger::chk
