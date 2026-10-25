#include "formats/wpn.hpp"

#include <cstring>

#include "formats/binary.hpp"

namespace harbinger::wpn {

namespace {

double f64(ByteReader& r) {
    const uint64_t lo = r.u32();
    const uint64_t bits = lo | (uint64_t(r.u32()) << 32);
    double v;
    std::memcpy(&v, &bits, sizeof v);
    return v;
}

}  // namespace

Weapon parse(const std::vector<uint8_t>& data) {
    ByteReader r(data, "wpn");
    Weapon w;
    w.version = f64(r);
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
    if (w.kind >= 1 && w.kind <= 3) {
        // GRangedWeapon_v7: the effect word, the sound, four words of light,
        // the energy, the cost, the recharge (a double), the pierce and a
        // last word.
        Ranged g;
        const uint16_t fx = r.u16();
        g.lob = (fx & 0x8000) != 0;
        g.effect = fx & 0x7fff;
        g.sound = r.u16();
        for (int16_t& c : g.light) c = r.i16();
        g.lightSize = r.i16();
        g.maxEnergy = r.i16();
        g.cost = r.i16();
        g.recharge = f64(r);
        g.pierce = r.i16();
        g.impactSound = r.u16();
        w.ranged = g;
    }
    r.expectEnd();
    return w;
}

}  // namespace harbinger::wpn
