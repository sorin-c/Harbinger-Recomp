#include "formats/gig.hpp"

#include "formats/binary.hpp"

namespace harbinger::gig {

uint32_t Group::totalWeight() const {
    uint32_t total = 0;
    for (const auto& e : entries) total += e.weight;
    return total;
}

Group parse(const std::vector<uint8_t>& data) {
    ByteReader r(data, "gig");
    Group g;
    r.skip(8);  // read first by FUN_004d6ec0; zero in every shipped group
    g.moneyMin = r.u8();
    g.moneyMax = r.u8();
    g.countMin = r.u8();
    g.countMax = r.u8();
    const uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; ++i) {
        Entry e;
        e.item = r.u16();
        e.weight = r.u16();
        g.entries.push_back(e);
    }
    r.expectEnd();
    return g;
}

}  // namespace harbinger::gig
