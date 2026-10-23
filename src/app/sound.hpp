// Audio output over SDL3: one streamed music track and short sound effects.
// Without an audio device everything is a silent no-op.
#pragma once

#include <SDL3/SDL.h>

#include <map>
#include <memory>

#include "audio/pcm.hpp"

namespace harbinger::app {

class SoundSystem {
public:
    SoundSystem();
    ~SoundSystem();
    SoundSystem(const SoundSystem&) = delete;
    SoundSystem& operator=(const SoundSystem&) = delete;

    bool enabled() const { return device_ != 0; }

    // Replaces the current music; `loop` restarts it at the end.
    void playMusic(std::unique_ptr<audio::Mp3Stream> music, bool loop);
    void stopMusic();

    // Plays a sound under `key`. Like the engine's interface sounds
    // (FUN_004c6720), playing a key that is still sounding restarts it.
    void play(int key, const audio::Pcm& pcm);

    void setMusicVolume(float gain);
    void setSoundVolume(float gain);

private:
    static void SDLCALL feedMusic(void* user, SDL_AudioStream* stream, int additional, int total);

    SDL_AudioDeviceID device_ = 0;
    SDL_AudioStream* music_ = nullptr;
    std::unique_ptr<audio::Mp3Stream> mp3_;
    bool loop_ = false;
    float musicGain_ = 1.0f, soundGain_ = 1.0f;
    std::map<int, SDL_AudioStream*> voices_;
};

}  // namespace harbinger::app
