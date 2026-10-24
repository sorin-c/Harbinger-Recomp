#include "engine/settings.hpp"

#include <cstring>

#include "formats/binary.hpp"

namespace harbinger {
namespace {

double readDouble(ByteReader& r) {
    const auto b = r.bytes(8);
    double v;
    std::memcpy(&v, b.data(), 8);
    return v;
}

void writeDouble(std::vector<uint8_t>& out, double v) {
    uint8_t b[8];
    std::memcpy(b, &v, 8);
    out.insert(out.end(), b, b + 8);
}

}  // namespace

// FUN_004ecc70 reads what FUN_004ecbe0 writes: three doubles, the primary and
// alternate key maps (FUN_004e1200), then the swap-mouse flag.
Settings Settings::parse(const std::vector<uint8_t>& data) {
    ByteReader r(data, "config.cfg");
    Settings s;
    s.music = readDouble(r);
    s.sound = readDouble(r);
    s.gamma = readDouble(r);
    const auto keys = r.bytes(kActions), alt = r.bytes(kActions);
    std::memcpy(s.keys.data(), keys.data(), kActions);
    std::memcpy(s.altKeys.data(), alt.data(), kActions);
    s.swapMouse = r.u8() != 0;
    r.expectEnd();
    return s;
}

std::vector<uint8_t> Settings::serialize() const {
    std::vector<uint8_t> out;
    writeDouble(out, music);
    writeDouble(out, sound);
    writeDouble(out, gamma);
    out.insert(out.end(), keys.begin(), keys.end());
    out.insert(out.end(), altKeys.begin(), altKeys.end());
    out.push_back(swapMouse ? 1 : 0);
    return out;
}

}  // namespace harbinger
