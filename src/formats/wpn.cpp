#include "formats/wpn.hpp"

#include <cstring>

#include "formats/binary.hpp"

namespace harbinger::wpn {

Weapon parse(const std::vector<uint8_t>& data) {
    ByteReader r(data, "wpn");
    Weapon w;
    const uint64_t lo = r.u32();
    const uint64_t bits = lo | (uint64_t(r.u32()) << 32);
    std::memcpy(&w.version, &bits, sizeof w.version);
    w.kind = r.u16();  // the weapon manager's factory word
    // GWeapon_v7 (0x51f780): from version 1.1, two bytes into +0x2c, +0x2d;
    // then FUN_0051fb20: u16 n and n (type, level, extra) triples.
    if (w.version > 1.1) {
        w.hitBonus = r.u8();
        w.bonus2d = r.u8();
    }
    const uint16_t n = r.u16();
    if (size_t(n) * 6 > r.remaining()) r.fail("implausible damage count");
    for (uint16_t i = 0; i < n; ++i) {
        Damage d;
        d.type = r.u16();
        d.level = r.u16();
        d.extra = r.u16();
        w.damage.push_back(d);
    }
    if (w.kind == 0 || w.kind == 4) r.expectEnd();
    w.rest = r.bytes(r.remaining());
    return w;
}

}  // namespace harbinger::wpn
