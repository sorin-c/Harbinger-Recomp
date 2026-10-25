#include "engine/display.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "engine/ini.hpp"

namespace harbinger {

bool parseSize(const std::string& text, int& w, int& h) {
    int a = 0, b = 0;
    char extra = 0;
    if (std::sscanf(text.c_str(), " %d x %d %c", &a, &b, &extra) != 2) return false;
    if (a < 320 || b < 240 || a > 16384 || b > 16384) return false;
    w = a, h = b;
    return true;
}

Display Display::parse(const std::string& text) {
    const Ini ini(text);
    Display d;
    d.fullscreen = ini.get("Display", "mode").value_or("window") == "fullscreen";
    d.aspect = ini.get("Display", "aspect").value_or("4:3") == "16:9" ? Aspect::Wide : Aspect::Classic;
    const std::string w = ini.get("Display", "width").value_or("800"), h = ini.get("Display", "height").value_or("600");
    parseSize(w + "x" + h, d.width, d.height);
    parseSize(ini.get("Display", "fullscreen").value_or("desktop"), d.fullW, d.fullH);
    return d;
}

std::string Display::serialize() const {
    const std::string full = fullW ? std::to_string(fullW) + "x" + std::to_string(fullH) : "desktop";
    return "[Display]\nmode=" + std::string(fullscreen ? "fullscreen" : "window") + "\nwidth=" + std::to_string(width) +
           "\nheight=" + std::to_string(height) + "\nfullscreen=" + full + "\naspect=" + harbinger::describe(aspect) + "\n";
}

std::string Display::describe() const {
    if (fullscreen) return fullW ? std::to_string(fullW) + " x " + std::to_string(fullH) + " Fullscreen" : "Fullscreen";
    return std::to_string(width) + " x " + std::to_string(height) + " Window";
}

const char* describe(Aspect a) { return a == Aspect::Wide ? "16:9" : "4:3"; }

std::vector<Display> displayChoices(Aspect aspect, int maxW, int maxH, const std::vector<std::pair<int, int>>& modes) {
    std::vector<Display> out;
    auto window = [&](int w, int h) { out.push_back({false, w, h, 0, 0, aspect}); };
    window(800, 600);
    window(1024, 768);
    if (aspect == Aspect::Classic) {
        for (int w : {1280, 1440, 1600, 1920, 2048, 2560, 2880})
            if (w <= maxW && w * 3 / 4 <= maxH) window(w, w * 3 / 4);
    } else {
        for (const auto& [w, h] : {std::pair{1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3200, 1800},
                                   {3840, 2160}})
            if (w <= maxW && h <= maxH) window(w, h);
    }
    out.push_back({true, 800, 600, 0, 0, aspect});
    for (const auto& [w, h] : modes) {
        const Display d{true, 800, 600, w, h, aspect};
        if (std::none_of(out.begin(), out.end(), [&](const Display& o) { return o.sameSize(d); })) out.push_back(d);
    }
    return out;
}

int gameViewWidth(int viewH, Aspect aspect) {
    if (aspect == Aspect::Classic) return viewH * 4 / 3;
    return int(std::lround(viewH * 16.0 / 9.0 / 2)) * 2;
}

}  // namespace harbinger
