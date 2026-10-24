#include <cstring>
#include <stdexcept>

#include "audio/pcm.hpp"
#include "formats/binary.hpp"

namespace harbinger::audio {

Pcm decodeWav(const std::vector<uint8_t>& data) {
    ByteReader r(data, "wav");
    const auto riff = r.bytes(4);
    r.u32();
    const auto wave = r.bytes(4);
    if (std::memcmp(riff.data(), "RIFF", 4) != 0 || std::memcmp(wave.data(), "WAVE", 4) != 0) r.fail("not RIFF/WAVE");
    Pcm pcm;
    int bits = 0;
    while (r.remaining() >= 8) {
        const auto id = r.bytes(4);
        const uint32_t size = r.u32();
        if (size > r.remaining()) r.fail("chunk overruns file");
        if (std::memcmp(id.data(), "fmt ", 4) == 0) {
            if (size < 16) r.fail("short fmt chunk");
            const uint16_t format = r.u16();
            pcm.channels = r.u16();
            pcm.rate = int(r.u32());
            r.u32();  // byte rate
            r.u16();  // block align
            bits = r.u16();
            r.bytes(size - 16);
            if (format != 1 || (bits != 8 && bits != 16) || pcm.channels < 1) r.fail("unsupported sample format");
        } else if (std::memcmp(id.data(), "data", 4) == 0) {
            if (!bits) r.fail("data before fmt");
            const auto raw = r.bytes(size);
            if (bits == 16) {
                pcm.samples.resize(raw.size() / 2);
                for (size_t i = 0; i < pcm.samples.size(); ++i)
                    pcm.samples[i] = int16_t(raw[2 * i] | (raw[2 * i + 1] << 8));
            } else {
                pcm.samples.resize(raw.size());
                for (size_t i = 0; i < raw.size(); ++i) pcm.samples[i] = int16_t((int(raw[i]) - 128) << 8);
            }
            return pcm;
        } else {
            r.bytes(size);
        }
        if (size & 1 && r.remaining()) r.u8();  // chunks are word-aligned
    }
    r.fail("no data chunk");
}

}  // namespace harbinger::audio
