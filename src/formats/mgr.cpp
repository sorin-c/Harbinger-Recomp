#include "formats/mgr.hpp"

#include "formats/binary.hpp"

namespace harbinger::mgr {

const std::string* Index::find(uint16_t id) const {
    for (const auto& e : entries)
        if (e.first == id) return &e.second;
    return nullptr;
}

Index parse(const std::vector<uint8_t>& data) {
    ByteReader r(data, "mgr");
    const uint32_t count = r.u32();
    if (count > r.remaining() / 6) r.fail("implausible entry count");
    Index index;
    index.entries.resize(count);
    for (auto& e : index.entries) e.first = r.u16();
    for (auto& e : index.entries) e.second = r.stringNulCounted();
    index.name = r.string();
    r.expectEnd();
    return index;
}

}  // namespace harbinger::mgr
