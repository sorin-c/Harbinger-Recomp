// harbinger-viewer: browse the game's sprites through the engine's VFS and
// renderer. A development tool and the first end-to-end test of the stack.
//
//   harbinger-viewer --data <install dir> [--prefix res/common/agents/]
//                    [--sprite <path>] [--screenshot out.bmp]
//
// Keys / gamepad: Left/Right or D-pad = previous/next file,
// Up/Down = jump 10 files, K or Y = cycle key-colour mode, Esc = quit.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/vfs.hpp"
#include "formats/gim.hpp"
#include "render/sprite.hpp"

using namespace harbinger;

namespace {

constexpr int kWidth = 1024, kHeight = 768;

struct Loaded {
    std::string path;
    std::string error;
    gim::Image image;
    std::vector<SDL_Texture*> frames;
};

void release(Loaded& l) {
    for (SDL_Texture* t : l.frames) SDL_DestroyTexture(t);
    l.frames.clear();
}

Loaded load(SDL_Renderer* ren, Vfs& vfs, const std::string& path, render::KeyMode mode) {
    Loaded l;
    l.path = path;
    try {
        auto data = vfs.read(path);
        if (!data) throw std::runtime_error("not found");
        l.image = gim::decode(*data);
        for (size_t f = 0; f < l.image.frames.size(); ++f) {
            auto px = render::toRgba(l.image, f, mode);
            SDL_Texture* t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC,
                                               l.image.width, l.image.height);
            if (!t) throw std::runtime_error(SDL_GetError());
            SDL_UpdateTexture(t, nullptr, px.data(), l.image.width * 4);
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
            l.frames.push_back(t);
        }
    } catch (const std::exception& ex) {
        release(l);
        l.error = ex.what();
    }
    return l;
}

const char* modeName(render::KeyMode m) {
    switch (m) {
        case render::KeyMode::Shadow: return "shadow";
        case render::KeyMode::Highlight: return "highlight";
        case render::KeyMode::Transparent: return "transparent";
        default: return "raw";
    }
}

void draw(SDL_Renderer* ren, const Loaded& l, size_t index, size_t count, render::KeyMode mode, Uint64 ticks) {
    SDL_SetRenderDrawColor(ren, 48, 52, 60, 255);
    SDL_RenderClear(ren);
    SDL_SetRenderDrawColor(ren, 230, 230, 230, 255);
    SDL_RenderDebugTextFormat(ren, 8, 8, "[%zu/%zu] %s", index + 1, count, l.path.c_str());
    if (!l.error.empty()) {
        SDL_RenderDebugTextFormat(ren, 8, 24, "error: %s", l.error.c_str());
        return;
    }
    const auto& im = l.image;
    SDL_RenderDebugTextFormat(ren, 8, 24, "%ux%u  encoding %u  frames %zu  origin %d,%d  key: %s", im.width,
                              im.height, im.encoding(), l.frames.size(), im.originX, im.originY, modeName(mode));
    if (l.frames.empty()) return;

    // Left: the animation at 2x (10 fps). Right: every frame in a grid.
    const float big = std::min(2.0f, std::min(380.0f / im.width, 600.0f / im.height));
    const size_t current = size_t(ticks / 100) % l.frames.size();
    SDL_FRect anim{20, 60, im.width * big, im.height * big};
    SDL_RenderTexture(ren, l.frames[current], nullptr, &anim);

    const float gx = 420, gy = 60, gw = kWidth - gx - 20, gh = kHeight - gy - 20;
    const size_t n = l.frames.size();
    const size_t cols = size_t(std::ceil(std::sqrt(double(n) * gw / gh * im.height / im.width)));
    const size_t rows = (n + cols - 1) / cols;
    const float s = std::min({1.0f, gw / (cols * im.width), gh / (rows * im.height)});
    for (size_t i = 0; i < n; ++i) {
        SDL_FRect r{gx + (i % cols) * im.width * s, gy + (i / cols) * im.height * s, im.width * s, im.height * s};
        SDL_SetRenderDrawColor(ren, 70, 76, 88, 255);
        SDL_RenderRect(ren, &r);
        SDL_RenderTexture(ren, l.frames[i], nullptr, &r);
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string dataDir, prefix = "res/common/agents/", sprite, screenshot;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        if (a == "--data") dataDir = argv[i + 1];
        else if (a == "--prefix") prefix = argv[i + 1];
        else if (a == "--sprite") sprite = argv[i + 1];
        else if (a == "--screenshot") screenshot = argv[i + 1];
    }
    if (dataDir.empty()) {
        std::fprintf(stderr, "usage: %s --data <Harbinger install dir> [--prefix p] [--sprite path] [--screenshot out.bmp]\n", argv[0]);
        return 2;
    }

    try {
        Vfs vfs(dataDir);
        std::vector<std::string> files;
        for (auto& p : vfs.list(prefix))
            if (p.size() > 4 && p.compare(p.size() - 4, 4, ".gim") == 0) files.push_back(p);
        if (!sprite.empty()) {
            const std::string want = sqh::normalizePath(sprite);
            auto it = std::find(files.begin(), files.end(), want);
            if (it == files.end()) files.insert(files.begin(), want), it = files.begin();
            std::rotate(files.begin(), it, files.end());
        }
        if (files.empty()) throw std::runtime_error("no .gim files under " + prefix);

        if (!screenshot.empty()) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) throw std::runtime_error(SDL_GetError());
        SDL_Window* win = nullptr;
        SDL_Renderer* ren = nullptr;
        if (!SDL_CreateWindowAndRenderer("Harbinger viewer", kWidth, kHeight, 0, &win, &ren))
            throw std::runtime_error(SDL_GetError());
        SDL_SetRenderVSync(ren, 1);

        render::KeyMode mode = render::KeyMode::Shadow;
        size_t index = 0;
        Loaded cur = load(ren, vfs, files[index], mode);

        if (!screenshot.empty()) {
            draw(ren, cur, index, files.size(), mode, 0);
            SDL_Surface* shot = SDL_RenderReadPixels(ren, nullptr);
            const bool ok = shot && SDL_SaveBMP(shot, screenshot.c_str());
            SDL_DestroySurface(shot);
            release(cur);
            SDL_Quit();
            if (!ok) throw std::runtime_error(SDL_GetError());
            std::printf("wrote %s (%s)\n", screenshot.c_str(), cur.error.empty() ? "ok" : cur.error.c_str());
            return cur.error.empty() ? 0 : 1;
        }

        auto step = [&](long delta) {
            const long n = long(files.size());
            index = size_t(((long(index) + delta) % n + n) % n);
            release(cur);
            cur = load(ren, vfs, files[index], mode);
        };
        bool running = true;
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) running = false;
                else if (e.type == SDL_EVENT_GAMEPAD_ADDED) SDL_OpenGamepad(e.gdevice.which);
                else if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                    const bool key = e.type == SDL_EVENT_KEY_DOWN;
                    const auto k = key ? e.key.key : 0;
                    const auto b = key ? -1 : int(e.gbutton.button);
                    if (k == SDLK_ESCAPE || b == SDL_GAMEPAD_BUTTON_BACK) running = false;
                    else if (k == SDLK_RIGHT || b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) step(1);
                    else if (k == SDLK_LEFT || b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) step(-1);
                    else if (k == SDLK_DOWN || b == SDL_GAMEPAD_BUTTON_DPAD_DOWN) step(10);
                    else if (k == SDLK_UP || b == SDL_GAMEPAD_BUTTON_DPAD_UP) step(-10);
                    else if (k == SDLK_K || b == SDL_GAMEPAD_BUTTON_NORTH) {
                        mode = mode == render::KeyMode::Shadow        ? render::KeyMode::Highlight
                               : mode == render::KeyMode::Highlight   ? render::KeyMode::Transparent
                               : mode == render::KeyMode::Transparent ? render::KeyMode::Raw
                                                                      : render::KeyMode::Shadow;
                        step(0);
                    }
                }
            }
            draw(ren, cur, index, files.size(), mode, SDL_GetTicks());
            SDL_RenderPresent(ren);
        }
        release(cur);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 0;
}
