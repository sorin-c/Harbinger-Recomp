#include "formats/agt.hpp"

#include <cstring>

#include "formats/binary.hpp"

namespace harbinger::agt {

namespace {

double f64(ByteReader& r) {
    const uint64_t lo = r.u32();
    const uint64_t bits = lo | (uint64_t(r.u32()) << 32);
    double v;
    std::memcpy(&v, &bits, sizeof v);
    return v;
}

}  // namespace

Template parse(const std::vector<uint8_t>& data) {
    ByteReader r(data, "agt");
    Template t;
    t.cls = r.u16();  // read by GAgentMgr_v5 (0x418670) to pick the class
    if (t.cls >= ClassCount) r.fail("unknown agent class");

    // Slot 9 (0x405dc0, shared by every class).
    t.type = r.u16();
    // FUN_00406a60: the header.
    t.version = f64(r);
    t.word10 = r.u16();
    t.d20 = f64(r);
    t.d28 = f64(r);
    int words = t.version >= 1.86 ? 10 : 9;
    if (t.version < 1.6) --words;
    if (t.version < 1.2) --words;
    for (int i = 0; i < words; ++i) t.stats.push_back(r.u16());
    t.name = r.string();
    // Slot 2, the class's own load: a type byte (checked against slot 7),
    // then for NPCs (0x408670) two words, the second from version 1.6, and
    // for players (0x4eb6d0 -> FUN_0050bc80) a u32 count of 6-byte entries.
    r.u8();
    if (t.isNpc()) {
        r.u16();
        if (t.version > 1.6) r.u16();
    } else if (t.cls == Player) {
        const uint32_t n = r.u32();
        if (n > r.remaining() / 6) r.fail("implausible player entry count");
        r.skip(size_t(n) * 6);
    }
    if (t.version >= 1.4) r.u16();  // agent+0x4c

    // FUN_00406100: the animations.
    const uint32_t anims = r.u32();
    if (anims > r.remaining() / 9) r.fail("implausible animation count");
    for (uint32_t i = 0; i < anims; ++i) {
        Anim a;
        a.kind = r.i16();
        a.param = r.i16();
        a.path = r.string();
        t.anims.push_back(std::move(a));
    }

    const size_t rest = r.pos();
    r.skip(6);           // agent+0x218
    r.u16(), r.u16();    // agent+0x46, +0x48
    if (t.version > 1.2) {
        const uint16_t lists = r.u16();  // FUN_00406c30 per list
        for (uint16_t i = 0; i < lists; ++i) r.skip(size_t(r.u32()) * 2);
    }
    if (t.version > 1.4) r.skip(size_t(r.u8()) * 4);
    if (t.version > 1.8) r.skip(t.version > 1.86 ? 11 : 10);  // FUN_00407170
    t.rest.assign(data.begin() + long(rest), data.begin() + long(r.pos()));
    r.expectEnd();
    return t;
}

}  // namespace harbinger::agt
