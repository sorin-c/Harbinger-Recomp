// Tests for Harbinger.ini reading and audio decoding. Real-data cases need HARBINGER_DATA.
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#include "audio/pcm.hpp"
#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/mgr.hpp"

using namespace harbinger;

static int failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

static void testIni() {
    const Ini ini("[Paths]\r\nFonts=res\\common\\managers\\font.mgr\r\n; comment\r\n"
                  "Accounts=first\r\nAccounts=second\r\n\r\n[Act1]\r\n Name = Act 1 \r\n");
    CHECK(ini.get("paths", "FONTS") == std::string("res\\common\\managers\\font.mgr"));
    CHECK(ini.get("Paths", "Accounts") == std::string("first"));  // first value wins
    CHECK(ini.get("Act1", "Name") == std::string("Act 1"));
    CHECK(!ini.get("Act1", "Fonts") && !ini.get("Nope", "Name"));
    bool threw = false;
    try {
        ini.require("Paths", "Missing");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

static std::vector<uint8_t> wav(uint16_t bits, std::vector<uint8_t> payload, bool extraChunk) {
    std::vector<uint8_t> d;
    auto u16 = [&](uint32_t v) { d.push_back(uint8_t(v)); d.push_back(uint8_t(v >> 8)); };
    auto u32 = [&](uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); };
    auto tag = [&](const char* t) { d.insert(d.end(), t, t + 4); };
    tag("RIFF");
    u32(0);
    tag("WAVE");
    tag("fmt ");
    u32(16);
    u16(1);
    u16(1);
    u32(22050);
    u32(22050 * bits / 8);
    u16(bits / 8);
    u16(bits);
    if (extraChunk) {
        tag("LIST");
        u32(3);
        d.insert(d.end(), {1, 2, 3, 0});  // odd size, padded
    }
    tag("data");
    u32(uint32_t(payload.size()));
    d.insert(d.end(), payload.begin(), payload.end());
    return d;
}

static void testWav() {
    auto pcm = audio::decodeWav(wav(16, {0x01, 0x00, 0xFF, 0xFF, 0x00, 0x80}, true));
    CHECK(pcm.rate == 22050 && pcm.channels == 1);
    CHECK(pcm.samples == (std::vector<int16_t>{1, -1, -32768}));
    pcm = audio::decodeWav(wav(8, {128, 255, 0}, false));
    CHECK(pcm.samples == (std::vector<int16_t>{0, 127 << 8, -128 << 8}));
    bool threw = false;
    try {
        audio::decodeWav({'R', 'I', 'F', 'F', 0, 0, 0, 0, 'A', 'V', 'I', ' '});
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

static void testRealData(const char* dir) {
    Vfs vfs(dir);
    const auto iniBytes = vfs.read("Harbinger.ini");
    CHECK(iniBytes.has_value());
    if (!iniBytes) return;
    const Ini ini = Ini::fromBytes(*iniBytes);

    // Every sound in sound.mgr decodes: 409 WAVs as PCM, 35 MP3s open. One
    // entry, 171 (Res\Common\Sounds\DreamCatcher.mp3), is not in any
    // shipped archive; Assets::sound returns nullptr for it.
    const auto sounds = mgr::parse(*vfs.read(ini.require("Paths", "Sounds")));
    size_t wavs = 0, mp3s = 0, missing = 0;
    for (const auto& [id, path] : sounds.entries) {
        const auto data = vfs.read(path);
        if (!data) {
            CHECK(id == 171);
            ++missing;
            continue;
        }
        try {
            if (path.size() > 4 && (path.compare(path.size() - 4, 4, ".mp3") == 0 || path.compare(path.size() - 4, 4, ".MP3") == 0)) {
                audio::Mp3Stream s(*data);
                ++mp3s;
            } else {
                const auto pcm = audio::decodeWav(*data);
                if (!pcm.samples.empty()) ++wavs;
            }
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "sound %u (%s): %s\n", unsigned(id), path.c_str(), ex.what());
            ++failures;
        }
    }
    CHECK(wavs == 409 && mp3s == 35 && missing == 1);

    // The menu music streams, and rewinding restarts it identically.
    audio::Mp3Stream music(*vfs.read(ini.require("Paths", "IntroMusic")));
    CHECK(music.rate() == 44100 && music.channels() == 2);
    std::vector<int16_t> a(4096 * 2), b(4096 * 2);
    CHECK(music.read(a.data(), 4096) == 4096);
    music.rewind();
    CHECK(music.read(b.data(), 4096) == 4096 && a == b);
}

int main() {
    testIni();
    testWav();
    if (const char* dir = std::getenv("HARBINGER_DATA"))
        testRealData(dir);
    else
        std::printf("HARBINGER_DATA not set; skipping real-data checks\n");
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
