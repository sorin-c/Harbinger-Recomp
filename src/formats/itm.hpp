// Item templates (.itm). Only the common header and the animated-item class
// (used by the front-end buttons) are decoded so far.
// Format notes and evidence: docs/formats/itm.md
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace harbinger::itm {

// Frame marks of an animated item (GAnimatedItem). See docs/formats/itm.md for
// how the engine steps through them on hover and click.
struct Animation {
    uint16_t loopStart = 0;  // hover loops over [loopStart, loopEnd)
    uint16_t loopEnd = 0;
    uint16_t pressEnd = 0;   // a click plays on from loopEnd and rests here
};

struct Item {
    uint16_t classId = 0;  // index into the engine's item factory (25 = GAnimatedItem)
    uint16_t unknown1 = 0;
    uint16_t unknown2 = 0;
    uint16_t image = 0;       // itemimg.mgr id
    uint16_t frameCount = 0;  // frames stacked vertically in the image
    uint16_t unknown5 = 0, unknown6 = 0;
    uint32_t unknown7 = 0;
    std::string name;
    std::optional<Animation> animation;  // set for class 25
    uint16_t trailer = 0;  // u16 read last by the base loader; 11 in every animated item
};

enum : uint16_t { AnimatedItem = 25 };

// Throws std::runtime_error on malformed data, trailing bytes, or a class whose
// body is not decoded yet.
Item parse(const std::vector<uint8_t>& data);

}  // namespace harbinger::itm
