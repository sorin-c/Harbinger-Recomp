// How the rebuild shows its screens: a window of some size, or fullscreen,
// and the game view's shape. Rebuild settings (the original switched display
// modes and was always 4:3), kept in their own file next to config.cfg.
//
// Widescreen: with the 16:9 aspect the game view keeps the original's height
// (768 with High Resolution, else 600) and is wider (1366, 1066), so more of
// the level shows at the sides. The front end stays 4:3, pillarboxed. Any
// window or fullscreen size can be used with either aspect; the view is
// scaled to fit it, with bars where the shapes differ.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace harbinger {

enum class Aspect { Classic, Wide };  // 4:3, 16:9

struct Display {
    bool fullscreen = false;
    int width = 800, height = 600;  // window size
    int fullW = 0, fullH = 0;       // fullscreen mode; 0 x 0 = the desktop's, borderless
    Aspect aspect = Aspect::Classic;

    // "[Display]" with mode=window|fullscreen, width, height (the window),
    // fullscreen=desktop|<w>x<h>, aspect=4:3|16:9. Missing or bad values keep
    // their defaults.
    static Display parse(const std::string& text);
    std::string serialize() const;
    // The size part: "1024 x 768 Window", "Fullscreen", "1920 x 1080 Fullscreen".
    std::string describe() const;

    // Same size and mode (the aspect is chosen separately).
    bool sameSize(const Display& o) const {
        if (fullscreen != o.fullscreen) return false;
        return fullscreen ? fullW == o.fullW && fullH == o.fullH : width == o.width && height == o.height;
    }
};

const char* describe(Aspect a);  // "4:3", "16:9"

// "1280x720", "1280 x 720": a size from 320x240 to 16384x16384.
bool parseSize(const std::string& text, int& w, int& h);

// The sizes offered for `aspect`: windows of that shape that fit in
// maxW x maxH (800x600 and 1024x768 always, whatever the aspect), then
// fullscreen at the desktop's size, then the fullscreen `modes` the display
// lists (largest first, as given).
std::vector<Display> displayChoices(Aspect aspect, int maxW, int maxH,
                                    const std::vector<std::pair<int, int>>& modes = {});

// The game view's width for a view `viewH` high: 4:3 (1024 at 768), or 16:9
// rounded to an even width (1366 at 768, 1066 at 600; the centre stays on a
// whole pixel).
int gameViewWidth(int viewH, Aspect aspect);

}  // namespace harbinger
