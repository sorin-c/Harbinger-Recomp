#include "formats/chk.hpp"

#include <algorithm>
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

// FUN_004e2d60: a layer count and that many (brush, tile) pairs. A cell holds
// at most two layers.
Cell cell(ByteReader& r) {
    Cell c;
    const uint16_t n = r.u16();
    if (n > 2) r.fail("cell with more than two layers");
    c.count = uint8_t(n);
    for (uint16_t i = 0; i < n; ++i) {
        c.layers[i].brush = r.u16();
        c.layers[i].tile = r.u16();
    }
    return c;
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

    // FUN_00448080: width * height cells.
    lv.cells.resize(size_t(lv.width) * lv.height);
    for (auto& c : lv.cells) c = cell(r);

    // FUN_00448080 goes on: a variant count, then for variants 1..n-1 a byte
    // size, a name and the cells they replace.
    const uint16_t variants = r.u16();
    if (!variants) r.fail("no base variant");
    for (uint16_t v = 1; v < variants; ++v) {
        r.u32();  // size, for skipping unselected variants
        Variant var;
        var.name = r.string();
        const uint16_t n = r.u16();
        for (uint16_t i = 0; i < n; ++i) {
            const uint16_t index = r.u16();
            if (index >= lv.cells.size()) r.fail("variant cell outside the map");
            var.cells.emplace_back(index, cell(r));
        }
        lv.variants.push_back(std::move(var));
    }
    lv.cellsEnd = r.pos();
    return lv;
}

bool Level::keeps(const GizmoRecord& g, size_t v) const {
    if (v >= instanceLists.size()) return false;
    const auto& list = instanceLists[v];
    return std::find(list.begin(), list.end(), g.instance) != list.end();
}

void parseGizmos(Level& lv, const std::vector<uint8_t>& data,
                 const std::function<const giz::Template*(uint16_t)>& lookup) {
    ByteReader r(data, "chk");
    r.skip(lv.cellsEnd);
    // FUN_004477e0: one list of instance ids per variant.
    lv.instanceLists.assign(lv.variants.size() + 1, {});
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
