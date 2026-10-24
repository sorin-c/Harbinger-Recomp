#include "formats/animgroup.hpp"

#include "formats/binary.hpp"

namespace harbinger::animgroup {

const Group* Index::find(const std::string& name) const {
    for (const auto& g : groups)
        if (g.name == name) return &g;
    return nullptr;
}

Index parse(const std::vector<uint8_t>& data) {
    ByteReader r(data, "animgroup");
    const uint32_t count = r.u32();
    if (count > r.remaining() / 8) r.fail("implausible group count");
    Index index;
    index.groups.resize(count);
    for (auto& g : index.groups) g.id = r.u16();
    for (auto& g : index.groups) g.name = r.stringNulCounted();
    index.name = r.string();
    const uint16_t states = r.u16();
    if (size_t(states) * count * 2 != r.remaining()) r.fail("table size does not match");
    for (auto& g : index.groups) {
        g.byState.resize(states);
        for (auto& a : g.byState) a = r.u16();
    }
    r.expectEnd();
    return index;
}

}  // namespace harbinger::animgroup
