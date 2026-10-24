#include "world/motion.hpp"

#include <array>
#include <cmath>

namespace harbinger::world {

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Tables {
    std::array<double, 256> sine{};
    std::array<uint8_t, 64> atan{};
    Tables() {
        // FUN_00528b40: sin(i * 2pi / 256), and atan(i / 64) in degrees,
        // rounded by adding 0.5 and scaled by 256 / 360 (0.7111), truncated.
        for (int i = 0; i < 256; ++i) sine[size_t(i)] = std::sin(i * (2 * kPi / 256));
        for (int i = 0; i < 64; ++i)
            atan[size_t(i)] = uint8_t(int((std::atan(i / 64.0) * (180 / kPi) + 0.5) * (256.0 / 360.0)));
    }
};

const Tables& tables() {
    static const Tables t;
    return t;
}

}  // namespace

uint8_t facingTowards(double dx, double dy) {
    const bool negX = dx < 0, negY = dy < 0;
    const double ax = std::fabs(dx), ay = std::fabs(dy);
    int a;
    if (ay < ax) {
        a = tables().atan[size_t(int(ay * 64 / ax)) & 0xff];
    } else if (ax < ay) {
        a = 64 - tables().atan[size_t(int(ax * 64 / ay)) & 0xff];
    } else {
        if (ax == 0) return 0;  // the original reports an error and returns 0
        a = 32;
    }
    if (negX) a = 128 - a;
    if (negY) a = -a;
    return uint8_t(a);
}

double facingSin(uint8_t facing) { return tables().sine[facing]; }
double facingCos(uint8_t facing) { return tables().sine[uint8_t(facing + 64)]; }

}  // namespace harbinger::world
