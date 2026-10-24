// The player's settings, stored in the original's config.cfg layout.
// Layout and evidence: docs/formats/config.md
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace harbinger {

struct Settings {
    // Slider values, 0..1 (Options screen).
    double music = 0.5, sound = 1.0, gamma = 0.5;
    // Action keys as DirectInput scan codes, in the key configuration
    // screen's order (Inventory, Automap, ... Manual Aim). 0 = unbound.
    static constexpr size_t kActions = 21;
    std::array<uint8_t, kActions> keys{}, altKeys{};
    bool swapMouse = false;

    // Throws std::runtime_error unless `data` is exactly one config.cfg.
    static Settings parse(const std::vector<uint8_t>& data);
    std::vector<uint8_t> serialize() const;

    // Music gain the original hands to Miles (FUN_00505840): value / 12.
    double musicGain() const { return music / 12.0; }
    // Display gamma the original applies (FUN_004a5fe0): 1.5 - value.
    double displayGamma() const { return 1.5 - gamma; }
};

}  // namespace harbinger
