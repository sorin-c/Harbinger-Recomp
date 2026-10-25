#include "formats/emt.hpp"

#include <cstring>

#include "formats/binary.hpp"

namespace harbinger::emt {

namespace {

double f64(ByteReader& r) {
    const uint64_t lo = r.u32();
    const uint64_t bits = lo | (uint64_t(r.u32()) << 32);
    double v;
    std::memcpy(&v, &bits, sizeof v);
    return v;
}

Basic readBasic(ByteReader& r) {
    // GBasicDesc_v3: u16 version, the life words, four angle bytes, 64 bytes
    // of doubles, the fan count and a flag byte.
    Basic b;
    b.version = r.u16();
    b.lifeMs = r.u32();
    b.lifeVarMs = r.u32();
    b.polar = r.u8();
    b.polarVar = r.u8();
    b.azimuth = r.u8();
    b.azimuthVar = r.u8();
    b.speed = f64(r);
    b.speedVar = f64(r);
    for (double& d : b.spread) d = f64(r);
    for (double& d : b.accel) d = f64(r);
    b.fan = r.i16();
    b.flags = r.u8();
    return b;
}

Billboard readBillboard(ByteReader& r) {
    Billboard b;
    // GAnimatedImageDesc_v4: version, the image path, a double, a flag byte,
    // then five words of frame layout.
    b.version = r.u16();
    b.image = r.string();
    b.frameTime = f64(r);
    b.imageFlags = r.u8();
    b.frameW = r.i16();
    b.frameH = r.i16();
    b.columns = r.i16();
    b.rows = r.i16();
    b.firstFrame = r.i16();
    // GBillboardDesc_v4: 32 bytes, a byte, 32 bytes, a flag byte.
    for (double& d : b.sizes) d = f64(r);
    b.byte50 = r.u8();
    for (double& d : b.more) d = f64(r);
    b.flags = r.u8();
    return b;
}

}  // namespace

Emitter parse(const std::vector<uint8_t>& data) {
    ByteReader r(data, "emt");
    Emitter e;
    // GEmitter_v6 (0x485b90).
    e.version = r.u16();
    e.wordD0 = r.u16();
    e.count = r.u32();
    e.rate = f64(r);
    e.d108 = f64(r);
    e.unknown = r.u32();
    e.byteD2 = r.u8();
    e.flags = r.u8();
    // FUN_00485ea0: the physics description by type.
    e.physicsType = r.u16();
    if (e.physicsType == 0) {
        // GPhysicsDesc_v3: a version and the two life words.
        Basic b;
        b.version = r.u16();
        b.lifeMs = r.u32();
        b.lifeVarMs = r.u32();
        e.basic = b;
    } else if (e.physicsType == 1) {
        e.basic = readBasic(r);
    } else if (e.physicsType > 3) {
        r.fail("unknown physics type");
    } else {
        return e;  // GCircularDesc, GLinearDesc: not decoded
    }
    // FUN_00486130: the particle description by type.
    e.particleType = r.u16();
    if (e.particleType != 1) return e;  // GRadialDesc, GLineDesc: not decoded
    e.billboard = readBillboard(r);
    if (e.version >= 2) {
        e.trail = r.u16();
        e.d110 = f64(r);
        e.d118 = f64(r);
    }
    r.expectEnd();
    e.complete = true;
    return e;
}

}  // namespace harbinger::emt
