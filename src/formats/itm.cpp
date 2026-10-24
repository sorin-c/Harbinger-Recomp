#include "formats/itm.hpp"

#include <vector>

#include "formats/binary.hpp"

namespace harbinger::itm {

Item parse(const std::vector<uint8_t>& data) {
    ByteReader r(data, "itm");
    Item it;
    // Common header (FUN_004d5c10): 14 bytes, a u32, the name.
    it.classId = r.u16();
    it.unknown1 = r.u16();
    it.unknown2 = r.u16();
    it.image = r.u16();
    it.frameCount = r.u16();
    it.unknown5 = r.u16();
    it.unknown6 = r.u16();
    it.unknown7 = r.u32();
    it.name = r.string();
    // A flag byte; when set, an optional sub-object follows (FUN_004d6fc0, not decoded).
    if (r.u8() != 0) r.fail("optional item block not decoded yet");
    // Class body (vtable slot 3).
    if (it.classId != AnimatedItem) r.fail("item class not decoded yet");
    Animation a;
    a.loopStart = r.u16();
    a.loopEnd = r.u16();
    a.pressEnd = r.u16();
    it.animation = a;
    it.trailer = r.u16();
    r.expectEnd();
    return it;
}

}  // namespace harbinger::itm
