#include "formats/anm.hpp"

#include "formats/binary.hpp"

namespace harbinger::anm {

Animation parse(const std::vector<uint8_t>& data) {
    ByteReader r(data, "anm");
    Animation a;
    // FUN_0042ab00: bit 0x10 flags per-frame words; a remaining value above 1
    // is the direction count (bit 0 cleared) plus a hotspot bit, otherwise
    // there are 16 directions and the value itself is the hotspot bit.
    a.header = r.u16();
    a.frameWords = a.header & 0x10;
    uint16_t h = a.header & ~0x10;
    size_t directions = 16;
    if (h > 1) {
        directions = h & 0xFFFE;
        h &= 1;
    }
    if (h) {  // FUN_00434b90
        const int16_t x = r.i16();
        a.hotspot = std::make_pair(x, r.i16());
    }
    // FUN_0042abe0: sound tags.
    const uint32_t sounds = r.u32();
    if (sounds > r.remaining() / 4) r.fail("implausible sound tag count");
    for (uint32_t i = 0; i < sounds; ++i) a.sounds.push_back(r.bytes(4));
    // FUN_0042a420: per direction a frame count and an action tag count, the
    // frame names (FUN_0042a850), the action tags (FUN_0042aa30) and the
    // per-frame words (FUN_0042a500).
    a.directions.resize(directions);
    for (auto& d : a.directions) {
        const uint32_t frames = r.u32(), actions = r.u32();
        if (frames > r.remaining() / 5 || actions > r.remaining() / 6) r.fail("implausible frame count");
        for (uint32_t i = 0; i < frames; ++i) d.frames.push_back(r.string());
        for (uint32_t i = 0; i < actions; ++i) d.actions.push_back(r.bytes(6));
        if (a.frameWords)
            for (uint32_t i = 0; i < frames; ++i) d.frameWords.push_back(r.u16());
    }
    const uint32_t extra = r.u32();
    if (extra > r.remaining() / 5) r.fail("implausible frame count");
    for (uint32_t i = 0; i < extra; ++i) a.extraFrames.push_back(r.string());
    r.expectEnd();
    return a;
}

size_t directionFor(uint8_t facing, size_t count) {
    // The tables at 0x556154 (16 directions) and 0x556354 (10) as the last
    // facing of each direction; facings past the last entry wrap to 0. The
    // steps are uneven because the view is isometric.
    static constexpr uint8_t last16[] = {8, 21, 31, 42, 55, 72, 95, 118, 136, 149, 159, 171, 182, 200, 223, 246};
    static constexpr uint8_t last10[] = {14, 31, 48, 77, 113, 142, 159, 176, 205, 241};
    auto find = [facing](const uint8_t* last, size_t n) -> size_t {
        for (size_t i = 0; i < n; ++i)
            if (facing <= last[i]) return i;
        return 0;
    };
    if (count == 16) return find(last16, 16);
    if (count == 10) return find(last10, 10);
    return 0;
}

const Direction* Animation::firstDirection() const {
    for (const auto& d : directions)
        if (!d.frames.empty()) return &d;
    return nullptr;
}

std::string sheetPath(const std::string& framePath) {
    const size_t file = framePath.find_last_of("\\/");
    if (file == std::string::npos || file == 0) return {};
    const size_t dir = framePath.find_last_of("\\/", file - 1);
    if (dir == std::string::npos || dir == 0) return {};
    const size_t anim = framePath.find_last_of("\\/", dir - 1);
    const size_t animStart = anim == std::string::npos ? 0 : anim + 1;
    const std::string animDir = framePath.substr(0, dir);
    const std::string animName = framePath.substr(animStart, dir - animStart);
    const std::string dirName = framePath.substr(dir + 1, file - dir - 1);
    return animDir + "\\fsp\\" + animName + dirName + "FSP.gim";
}

}  // namespace harbinger::anm
