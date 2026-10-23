// Interface layout files (.itf): the controls of one screen or HUD panel.
// Format notes and evidence: docs/formats/itf.md
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace harbinger::itf {

// Layout block of the text controls (StaticTextBox and the Stats* labels).
struct TextBox {
    uint16_t width = 0, height = 0;
    uint16_t font = 0;      // font.mgr id for ordinary lines
    uint16_t fontAlt = 0;   // lines starting with '&' (and the highlighted line)
    uint16_t fontStar = 0;  // lines starting with '*'
    uint16_t align = 0;     // 0 = left, 0x10 = right, 0x20 = centred
    uint16_t padX = 0, padY = 0;
};

struct Control {
    uint16_t classId = 0;  // index into the engine's control factory, see className()
    int16_t x = 0, y = 0, z = 0;
    std::string name;
    std::vector<std::string> images;
    std::string extra;  // e.g. a hit mask image for TextBtn
    // Class-specific fields, in file order (u16 and u32 values widened).
    std::vector<uint32_t> values;
    std::optional<TextBox> textBox;
    std::string text;  // trailing string of classes that have one (label, caption)
};

struct Interface {
    int32_t header[3] = {};  // meaning not confirmed; [0],[1] look like a panel position
    uint16_t kind = 0;       // selects how the engine positions the panel
    std::vector<Control> controls;
    // UI sound ids by event slot (9 = hover, 10 = click on AnimatedBtn); empty if absent.
    std::vector<uint16_t> sounds;
};

// Control class ids used by the files (the factory has 0x57 entries).
enum Class : uint16_t {
    Backgd = 0x00,
    Button = 0x03,
    StaticTextBox = 0x0D,
    TextBtn = 0x3A,
    AnimatedBtn = 0x50,
};

// Engine class name without the "GCtrl" prefix, or nullptr for an unknown id.
const char* className(uint16_t classId);

// Throws std::runtime_error on malformed data, unknown classes or trailing bytes.
Interface parse(const std::vector<uint8_t>& data);

}  // namespace harbinger::itf
