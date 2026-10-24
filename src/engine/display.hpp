// How the rebuild shows its 800x600 screens: a window of some size, or
// fullscreen. A rebuild setting (the original switched display modes), kept
// in its own file next to config.cfg.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace harbinger {

struct Display {
    bool fullscreen = false;
    int width = 800, height = 600;  // window size when not fullscreen

    // "[Display]" with mode=window|fullscreen, width, height. Missing or bad
    // values keep their defaults.
    static Display parse(const std::string& text);
    std::string serialize() const;
    std::string describe() const;  // "1024 x 768 window", "Fullscreen"

    bool operator==(const Display& o) const {
        return fullscreen == o.fullscreen && (fullscreen || (width == o.width && height == o.height));
    }
};

// The choices offered: 4:3 windows from 800x600 up to what fits in
// maxW x maxH (always at least 800x600), then fullscreen.
std::vector<Display> displayChoices(int maxW, int maxH);

}  // namespace harbinger
