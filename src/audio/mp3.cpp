#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#include <stdexcept>

#include "audio/pcm.hpp"

namespace harbinger::audio {

struct Mp3Stream::Impl {
    drmp3 mp3;
};

Mp3Stream::Mp3Stream(std::vector<uint8_t> data) : data_(std::move(data)), impl_(std::make_unique<Impl>()) {
    if (!drmp3_init_memory(&impl_->mp3, data_.data(), data_.size(), nullptr)) throw std::runtime_error("mp3: cannot decode");
    rate_ = int(impl_->mp3.sampleRate);
    channels_ = int(impl_->mp3.channels);
}

Mp3Stream::~Mp3Stream() { drmp3_uninit(&impl_->mp3); }

size_t Mp3Stream::read(int16_t* out, size_t frames) {
    return size_t(drmp3_read_pcm_frames_s16(&impl_->mp3, frames, out));
}

void Mp3Stream::rewind() { drmp3_seek_to_pcm_frame(&impl_->mp3, 0); }

}  // namespace harbinger::audio
