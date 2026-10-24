// Decoded audio: 16-bit signed PCM, interleaved.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace harbinger::audio {

struct Pcm {
    int rate = 0;
    int channels = 0;
    std::vector<int16_t> samples;  // frames * channels
};

// Uncompressed RIFF/WAVE (8- or 16-bit PCM, as every game sound is).
// Throws std::runtime_error for anything else.
Pcm decodeWav(const std::vector<uint8_t>& data);

// Streaming MP3 decoder (music is too long to decode up front).
class Mp3Stream {
public:
    // Takes ownership of the file bytes. Throws std::runtime_error if they are not MP3.
    explicit Mp3Stream(std::vector<uint8_t> data);
    ~Mp3Stream();
    Mp3Stream(const Mp3Stream&) = delete;
    Mp3Stream& operator=(const Mp3Stream&) = delete;

    int rate() const { return rate_; }
    int channels() const { return channels_; }
    // Decodes up to `frames` frames into `out` (frames * channels samples);
    // returns the frames written, 0 at the end.
    size_t read(int16_t* out, size_t frames);
    void rewind();

private:
    struct Impl;
    std::vector<uint8_t> data_;
    std::unique_ptr<Impl> impl_;
    int rate_ = 0, channels_ = 0;
};

}  // namespace harbinger::audio
