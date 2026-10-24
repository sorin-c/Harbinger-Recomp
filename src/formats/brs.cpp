#include "formats/brs.hpp"

#include "formats/binary.hpp"

namespace harbinger::brs {

Brush parse(const std::vector<uint8_t>& data) {
    ByteReader r(data, "brs");
    Brush b;
    b.name = r.string();
    b.kind = r.u16();
    b.height = r.u16();
    b.walkable = r.u8();
    for (auto& group : b.groups) {
        const uint16_t count = r.u16();
        if (count > r.remaining() / 7) r.fail("implausible tile count");
        group.resize(count);
        for (auto& t : group) {
            t.id = r.u16();
            t.path = r.string();
        }
    }
    // The engine reads this list only in its editor build; every shipped file has it.
    if (r.remaining()) {
        const uint16_t count = r.u16();
        for (uint16_t i = 0; i < count; ++i) {
            const uint16_t id = r.u16();
            b.categories.emplace_back(id, r.string());
        }
    }
    r.expectEnd();
    return b;
}

const Tile* find(const Brush& brush, uint16_t id) {
    const unsigned group = id >> 12;
    if (group >= GroupCount) return nullptr;
    for (const auto& t : brush.groups[group]) {
        if (group == Centre) {
            const unsigned count = (t.id & 0xF) ? (t.id & 0xF) : 16;
            if ((id & 0xFFF0) == (t.id & 0xFFF0) && (id & 0xF) < count) return &t;
        } else if (t.id == id) {
            return &t;
        }
    }
    return nullptr;
}

}  // namespace harbinger::brs
