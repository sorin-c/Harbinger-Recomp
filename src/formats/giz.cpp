#include "formats/giz.hpp"

#include <cstring>
#include <stdexcept>

#include "formats/binary.hpp"

namespace harbinger::giz {

namespace {

double f64(ByteReader& r) {
    const uint64_t lo = r.u32();
    const uint64_t bits = lo | (uint64_t(r.u32()) << 32);
    double v;
    std::memcpy(&v, &bits, sizeof v);
    return v;
}

// u16 length that counts the NUL, then the characters (FUN_004b7350, FUN_004b5c00).
std::string shortString(ByteReader& r) {
    const uint16_t n = r.u16();
    if (!n) r.fail("empty string");
    auto bytes = r.bytes(n);
    if (bytes.back() != 0) r.fail("string not NUL-terminated");
    return std::string(bytes.begin(), bytes.end() - 1);
}

void raw(ByteReader& r, std::vector<uint8_t>& out, size_t n) {
    auto b = r.bytes(n);
    out.insert(out.end(), b.begin(), b.end());
}

}  // namespace

Template parse(const std::vector<uint8_t>& data) {
    ByteReader r(data, "giz");
    Template t;
    // FUN_004b7350: the header that picks the class.
    t.version = f64(r);
    t.name = shortString(r);
    t.flags = r.u16();
    t.cls = uint8_t(t.flags);
    if (t.cls >= ClassCount) r.fail("unknown gizmo class");
    t.layout = r.u16();
    t.unknownByte = r.u8();

    // GAnimatedGizmo_v24 (0x430d50; GStaticGizmo's 0x50b540 reads the same):
    // masks, then frames.
    const uint16_t maskCount = r.u16();
    const uint16_t frameCount = r.u16();
    for (uint16_t i = 0; i < maskCount; ++i) {
        Mask m;
        m.path = shortString(r);  // FUN_004b5c00
        m.rows = r.u16();
        const uint16_t n = r.u16();  // FUN_004b5ca0: 10-byte cells
        for (uint16_t k = 0; k < n; ++k) {
            MaskCell c;
            c.dx = r.i16(), c.dy = r.i16(), c.kind = r.i16(), c.a = r.i16(), c.b = r.i16();
            m.cells.push_back(c);
        }
        t.masks.push_back(std::move(m));
    }
    for (uint16_t i = 0; i < frameCount; ++i) {
        Frame f;
        f.mask = r.u16();
        if (f.mask >= maskCount) r.fail("frame names a missing mask");
        // FUN_004b53e0
        f.x = r.i16(), f.y = r.i16();
        f.durationMs = r.u16();
        f.code = r.u16();
        f.path = shortString(r);
        const uint8_t hasRect = r.u8();
        if (hasRect == 1) {  // FUN_004ba840
            f.hasRect = true;
            auto b = r.bytes(8);
            std::memcpy(f.rect, b.data(), 8);
        }
        t.frames.push_back(std::move(f));
    }
    if (t.version > 1.03) t.point[0] = r.i16(), t.point[1] = r.i16();  // FUN_004b2260

    // The class's own load (vtable slot 4). Every one starts with
    // GAnimatedGizmo_v4 (0x4b21c0), which reads the offset only when the
    // header's 0x2000 flag reset it to zero.
    auto base = [&] {
        if (t.flags & HasOffset) t.offset[0] = r.i16(), t.offset[1] = r.i16();
    };
    switch (t.cls) {
    case Chest: case Barrel: case TrashPile: case Console: case CompuConsole: case KeyConsole: case Container:
        base();  // GBarrel_v4 (0x4bca70); consoles (0x453800) and containers (0x456020) build on it
        if (t.version > 1.01) t.label = r.string();
        if (t.cls == Container) raw(r, t.classData, 4);
        break;
    case Bridge:
        base();
        raw(r, t.classData, 2);  // 0x43c7a0
        break;
    case ChargingStn:
        raw(r, t.classData, 8);  // 0x443e80 -> FUN_0051a350
        break;
    case Umbilical:
        base();
        raw(r, t.classData, 8);  // 0x51d1c0 -> FUN_0051a350
        break;
    case Incubator: case RandomPause:
        raw(r, t.classData, 8);  // 0x4f9790
        break;
    case Pillar:
        raw(r, t.classData, 8);  // 0x4ea800
        break;
    case Portal:
        base();
        raw(r, t.classData, 16);  // 0x51e850: FUN_0051a350 twice
        break;
    case Spawn: {
        base();
        const uint16_t n = r.u16();  // 0x5085b0: becomes gizmo+0xcc, the plug's slot count
        for (uint16_t i = 0; i < n; ++i) {
            SpawnPoint s;
            s.a = r.i16(), s.b = r.i16(), s.c = r.i16();
            t.spawns.push_back(s);
        }
        break;
    }
    case Flicker:
        base();
        raw(r, t.classData, 12);  // 0x490bc0
        break;
    default:
        base();  // Static, Looped, Animated, UmbilicalShell, Breaker, Splitter, TwoWay
        break;
    }

    if (t.flags & HasLight) {
        // gizmo+100: two bytes, 0x30, 0x18, then FUN_004b3af0's two lists of
        // 26-byte entries.
        raw(r, t.light, 2 + 0x30 + 0x18);
        for (int list = 0; list < 2; ++list) {
            const uint16_t n = r.u16();
            t.light.push_back(uint8_t(n)), t.light.push_back(uint8_t(n >> 8));
            raw(r, t.light, size_t(n) * 26);
        }
    }
    if (t.flags & HasScript) {
        // FUN_004b3310: blocks of (id, 16-byte entries, 12-byte entries), then a word.
        const size_t start = r.pos();
        const uint16_t blocks = r.u16();
        for (uint16_t i = 0; i < blocks; ++i) {
            r.u16();
            r.skip(size_t(r.u16()) * 16);
            r.skip(size_t(r.u16()) * 12);
        }
        r.u16();
        t.script.assign(data.begin() + long(start), data.begin() + long(r.pos()));
    }
    r.expectEnd();
    return t;
}

size_t Template::recordExtra(const uint8_t* p, size_t avail) const {
    size_t n = 0;
    auto need = [&](size_t k) {
        if (k > avail - n) throw std::runtime_error("chk: truncated gizmo record (" + name + ")");
        n += k;
    };
    // FUN_004b8bf0: a flag byte, then an engine string when it is 1.
    auto label = [&] {
        need(1);
        if (p[n - 1] == 1) {
            need(4);
            const uint32_t len = uint32_t(p[n - 4]) | uint32_t(p[n - 3]) << 8 | uint32_t(p[n - 2]) << 16 |
                                 uint32_t(p[n - 1]) << 24;
            need(size_t(len) + 1);
        }
    };
    // Each plug's chunk load (vtable slot 22) at chunk version 1.23, after the
    // six bytes of GFakeGizmoPlug_v22 (0x4b9440).
    switch (cls) {
    case Looped: case Looped2: need(6); break;                   // 0x4e1df0
    case Chest: case Barrel: case TrashPile: label(); need(8); break;  // 0x445140
    case Bridge: case TwoWay: need(2); break;                    // 0x43c830
    case Console: need(6); label(); need(2); break;              // 0x454480
    case ChargingStn: need(8); break;                            // 0x444260
    case Umbilical: need(10); break;                             // 0x51dca0
    case Incubator: case RandomPause: need(2); break;            // 0x4f9df0
    case Pillar: need(10); break;                                // 0x4eac30
    case Breaker: need(4); break;                                // 0x439bf0
    case Splitter: need(8); break;                               // 0x50a450
    case CompuConsole: case KeyConsole: need(6); label(); need(4); break;  // 0x454eb0, 0x4557f0
    case Portal: need(14); break;                                // 0x51f4d0
    case Spawn: need(8 + 2 * spawns.size()); break;              // 0x509690: splitter, then a word per slot
    case Container: need(2); label(); need(2); break;            // 0x455d00
    default: break;  // Static, Animated, UmbilicalShell, Flicker: nothing more
    }
    return n;
}

}  // namespace harbinger::giz
