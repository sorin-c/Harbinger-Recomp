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
    // GAgentMgr_v13 (0x4b02b0): a negative count is negated and followed by
    // a word the loader skips (the speech indexes use this form).
    int64_t count = int32_t(r.u32());
    if (count < 0) {
        count = -count;
        r.u16();
    }
    if (uint64_t(count) > r.remaining() / 6) r.fail("implausible entry count");
    Index index;
    index.entries.resize(count);
    for (auto& e : index.entries) e.first = r.u16();
    for (auto& e : index.entries) e.second = r.stringNulCounted();
    index.name = r.string();
    r.expectEnd();
    return index;
}

}  // namespace harbinger::mgr
