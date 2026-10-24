#include "formats/acr.hpp"

#include "formats/binary.hpp"

namespace harbinger::acr {

Cursor parse(const std::vector<uint8_t>& data) {
    ByteReader r(data, "acr");
    Cursor c;
    c.frameDelay = r.u32();
    c.hotX = r.i16();
    c.hotY = r.i16();
    const uint16_t count = r.u16();
    for (uint16_t i = 0; i < count; ++i) c.frames.push_back(r.string());
    r.expectEnd();
    return c;
}

}  // namespace harbinger::acr
