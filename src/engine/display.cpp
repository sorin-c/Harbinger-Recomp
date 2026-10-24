#include "engine/display.hpp"

#include "engine/ini.hpp"

namespace harbinger {

Display Display::parse(const std::string& text) {
    const Ini ini(text);
    Display d;
    d.fullscreen = ini.get("Display", "mode").value_or("window") == "fullscreen";
    try {
        const int w = std::stoi(ini.get("Display", "width").value_or("800"));
        const int h = std::stoi(ini.get("Display", "height").value_or("600"));
        if (w >= 320 && h >= 240 && w <= 16384 && h <= 16384) d.width = w, d.height = h;
    } catch (const std::exception&) {
    }
    return d;
}

std::string Display::serialize() const {
    return "[Display]\nmode=" + std::string(fullscreen ? "fullscreen" : "window") + "\nwidth=" + std::to_string(width) +
           "\nheight=" + std::to_string(height) + "\n";
}

std::string Display::describe() const {
    if (fullscreen) return "Fullscreen";
    return std::to_string(width) + " x " + std::to_string(height) + " Window";
}

std::vector<Display> displayChoices(int maxW, int maxH) {
    std::vector<Display> out;
    for (int w : {800, 1024, 1280, 1440, 1600, 1920, 2048, 2560, 2880})
        if (w == 800 || (w <= maxW && w * 3 / 4 <= maxH)) out.push_back({false, w, w * 3 / 4});
    out.push_back({true, 800, 600});
    return out;
}

}  // namespace harbinger
