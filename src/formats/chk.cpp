#include "formats/chk.hpp"

#include <algorithm>
#include <cstring>

#include "formats/binary.hpp"
#include "formats/agt.hpp"
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

namespace {
bool listed(const std::vector<std::vector<uint16_t>>& lists, size_t v, uint16_t instance) {
    if (v >= lists.size()) return false;
    return std::find(lists[v].begin(), lists[v].end(), instance) != lists[v].end();
}
}  // namespace

bool Level::keeps(const GizmoRecord& g, size_t v) const { return listed(instanceLists, v, g.instance); }
bool Level::keeps(const AgentRecord& a, size_t v) const { return listed(agentLists, v, a.instance); }

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

void parseAgents(Level& lv, const std::vector<uint8_t>& data,
                 const std::function<const agt::Template*(uint16_t)>& lookup) {
    ByteReader r(data, "chk");
    r.skip(lv.gizmosEnd);
    lv.agentLists.assign(lv.variants.size() + 1, {});
    for (auto& list : lv.agentLists) {
        const uint16_t n = r.u16();
        for (uint16_t i = 0; i < n; ++i) list.push_back(r.u16());
    }
    const uint32_t count = r.u32();
    if (count > r.remaining() / 30) r.fail("implausible agent count");
    lv.agents.clear();
    for (uint32_t i = 0; i < count; ++i) {
        AgentRecord a;
        a.agent = r.u16();
        const agt::Template* t = lookup(a.agent);
        if (!t) r.fail("agent id missing from agent.mgr");
        // 0x420520: six bytes, the slot-18 words, the position, the label
        // (slot 7), then a byte and two words (version >= 1.22).
        a.instance = r.u16();
        a.facing = r.u8();
        a.unknown = r.u8();
        const uint16_t slots = r.u16();
        for (uint16_t k = 0; k < slots; ++k) a.slots.push_back(r.u16());
        a.x = f64(r);
        a.y = f64(r);
        if (!(a.x >= 0 && a.x < lv.width && a.y >= 0 && a.y < lv.height)) r.fail("agent outside the map");
        if (r.u8() == 1) a.label = r.string();
        a.flag = r.u8();
        a.words[0] = r.u16();
        a.words[1] = r.u16();
        if (t->recordHasExtraWord()) a.extra = r.u16();  // 0x4e6670
        lv.agents.push_back(std::move(a));
    }
    lv.agentsEnd = r.pos();
}

namespace {

void readTrigger(ByteReader& r, Trigger& t) {
    t.x = f64(r);
    t.y = f64(r);
    t.radius = f64(r);
    t.unknown = r.u16();
    t.id = r.u16();
}

SoundInfo readSound(ByteReader& r) {
    SoundInfo s;
    s.x = f64(r);
    s.y = f64(r);
    s.sound = r.u16();
    s.unknown = r.u16();
    s.unknown2 = r.u32();
    return s;
}

template <typename Count>
std::vector<uint16_t> readIds(ByteReader& r, Count n) {
    if (n > r.remaining() / 2) r.fail("implausible count");
    std::vector<uint16_t> ids(n);
    for (auto& id : ids) id = r.u16();
    return ids;
}

}  // namespace

void parseRest(Level& lv, const std::vector<uint8_t>& data) {
    ByteReader r(data, "chk");
    r.skip(lv.agentsEnd);

    // FUN_0051a6a0: two counts, the event triggers, then the gizmo triggers.
    const uint32_t events = r.u32(), gizmoTriggers = r.u32();
    if (events > r.remaining() / 40 || gizmoTriggers > r.remaining() / 30) r.fail("implausible trigger count");
    lv.eventTriggers.clear();
    for (uint32_t i = 0; i < events; ++i) {
        EventTrigger t;
        readTrigger(r, t);
        t.radius2 = f64(r);
        const uint16_t n = r.u16();  // FUN_0048ca20: the ids, then 8 bytes each
        const auto ids = readIds(r, n);
        for (uint16_t k = 0; k < n; ++k) {
            std::array<uint8_t, 8> b{};
            for (auto& x : b) x = r.u8();
            t.gizmos.emplace_back(ids[k], b);
        }
        t.word = r.u32();
        t.flag = r.u8();
        t.word2 = r.u16();
        const uint16_t sounds = r.u16();  // FUN_0048cd20
        if (sounds > r.remaining() / 26) r.fail("implausible sound count");
        for (uint16_t k = 0; k < sounds; ++k) {
            const uint16_t id = r.u16();
            t.sounds.emplace_back(id, readSound(r));
        }
        t.ids = readIds(r, r.u16());  // FUN_0048cbc0
        // FUN_0048ce30: nothing up to 1.17; a script inline up to 1.19
        // (not decoded, no shipped level has one); then a script id.
        if (lv.version > 1.19) t.script = r.u16();
        else if (lv.version > 1.17) r.fail("inline event scripts are not supported");
        lv.eventTriggers.push_back(std::move(t));
    }
    lv.gizmoTriggers.clear();
    for (uint32_t i = 0; i < gizmoTriggers; ++i) {
        GizmoTrigger t;
        readTrigger(r, t);
        t.gizmo = r.u16();
        lv.gizmoTriggers.push_back(t);
    }

    // FUN_00504a90: placed sounds.
    const uint32_t sounds = r.u32();
    if (sounds > r.remaining() / 24) r.fail("implausible sound count");
    lv.sounds.clear();
    for (uint32_t i = 0; i < sounds; ++i) lv.sounds.push_back(readSound(r));

    // FUN_0044ab80: two lists keyed by gizmo instance, each ended by 0.
    lv.attachments.clear();
    while (const uint16_t id = r.u16()) lv.attachments.emplace_back(id, readIds(r, r.u32()));
    lv.poweredBy.clear();
    while (const uint16_t id = r.u16()) lv.poweredBy.emplace_back(id, readIds(r, r.u16()));
    // The chunk's own slot 3 (GChunk_v3).
    lv.powerWord = r.u16();
    const uint16_t sources = r.u16();
    if (sources > r.remaining() / 4) r.fail("implausible power source count");
    lv.powerSources.clear();
    for (uint16_t i = 0; i < sources; ++i) {
        const uint16_t id = r.u16();
        lv.powerSources.emplace_back(id, r.u16());
    }

    // FUN_00436460: beacons.
    const uint32_t beacons = r.u32();
    if (beacons > r.remaining() / 22) r.fail("implausible beacon count");
    lv.beacons.clear();
    for (uint32_t i = 0; i < beacons; ++i) {
        Beacon b;
        b.x = f64(r);
        b.y = f64(r);
        b.room = r.u16();
        const uint32_t n = r.u32();
        if (n > r.remaining() / 6) r.fail("implausible beacon link count");
        const auto ids = readIds(r, n);
        for (uint32_t k = 0; k < n; ++k) b.links.emplace_back(ids[k], r.u32());
        lv.beacons.push_back(std::move(b));
    }

    // FUN_004fb860: rooms.
    const uint32_t rooms = r.u32();
    if (rooms > r.remaining() / 18) r.fail("implausible room count");
    lv.rooms.clear();
    for (uint32_t i = 0; i < rooms; ++i) {
        Room m;
        m.id = r.u16();
        for (auto& w : m.a) w = r.u16();
        for (auto& w : m.b) w = r.u16();
        lv.rooms.push_back(m);
    }
    r.expectEnd();
}

}  // namespace harbinger::chk
