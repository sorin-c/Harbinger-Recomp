#include "app/sound.hpp"

#include <algorithm>
#include <vector>

namespace harbinger::app {

SoundSystem::SoundSystem() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("audio disabled: %s", SDL_GetError());
        return;
    }
    device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!device_) SDL_Log("audio disabled: %s", SDL_GetError());
}

SoundSystem::~SoundSystem() {
    stopMusic();
    for (auto& [key, s] : voices_) SDL_DestroyAudioStream(s);
    if (device_) SDL_CloseAudioDevice(device_);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

// Runs on SDL's audio thread; the stream is locked while it does.
void SDLCALL SoundSystem::feedMusic(void* user, SDL_AudioStream* stream, int additional, int) {
    auto* self = static_cast<SoundSystem*>(user);
    const int channels = self->mp3_->channels();
    std::vector<int16_t> buf(4096 * size_t(channels));
    while (additional > 0) {
        const size_t want = std::min<size_t>(4096, size_t(additional) / (2 * size_t(channels)) + 1);
        size_t got = self->mp3_->read(buf.data(), want);
        if (got == 0) {
            if (!self->loop_) return;
            self->mp3_->rewind();
            got = self->mp3_->read(buf.data(), want);
            if (got == 0) return;
        }
        const int bytes = int(got * size_t(channels) * 2);
        SDL_PutAudioStreamData(stream, buf.data(), bytes);
        additional -= bytes;
    }
}

void SoundSystem::playMusic(std::unique_ptr<audio::Mp3Stream> music, bool loop) {
    stopMusic();
    if (!device_ || !music) return;
    mp3_ = std::move(music);
    loop_ = loop;
    const SDL_AudioSpec spec{SDL_AUDIO_S16, mp3_->channels(), mp3_->rate()};
    music_ = SDL_CreateAudioStream(&spec, nullptr);
    if (!music_) {
        SDL_Log("music: %s", SDL_GetError());
        mp3_.reset();
        return;
    }
    SDL_SetAudioStreamGain(music_, musicGain_);
    SDL_SetAudioStreamGetCallback(music_, feedMusic, this);
    SDL_BindAudioStream(device_, music_);
}

void SoundSystem::stopMusic() {
    if (music_) SDL_DestroyAudioStream(music_);  // unbinds; waits for a running callback
    music_ = nullptr;
    mp3_.reset();
}

bool SoundSystem::playing(int key) const {
    const auto it = voices_.find(key);
    return it != voices_.end() && SDL_GetAudioStreamQueued(it->second) > 0;
}

void SoundSystem::stop(int key) {
    if (const auto it = voices_.find(key); it != voices_.end()) SDL_ClearAudioStream(it->second);
}

void SoundSystem::play(int key, const audio::Pcm& pcm, bool restart) {
    if (!device_ || pcm.samples.empty() || (!restart && playing(key))) return;
    const SDL_AudioSpec spec{SDL_AUDIO_S16, pcm.channels, pcm.rate};
    SDL_AudioStream*& s = voices_[key];
    if (s) {
        SDL_ClearAudioStream(s);
        SDL_SetAudioStreamFormat(s, &spec, nullptr);
    } else {
        s = SDL_CreateAudioStream(&spec, nullptr);
        if (!s) {
            voices_.erase(key);
            return;
        }
        SDL_BindAudioStream(device_, s);
    }
    SDL_SetAudioStreamGain(s, soundGain_);
    SDL_PutAudioStreamData(s, pcm.samples.data(), int(pcm.samples.size() * 2));
    SDL_FlushAudioStream(s);
}

void SoundSystem::setMusicVolume(float gain) {
    musicGain_ = gain;
    if (music_) SDL_SetAudioStreamGain(music_, gain);
}

void SoundSystem::setSoundVolume(float gain) {
    soundGain_ = gain;
    for (auto& [key, s] : voices_) SDL_SetAudioStreamGain(s, gain);
}

}  // namespace harbinger::app
