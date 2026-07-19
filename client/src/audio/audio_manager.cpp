#include "audio/audio_manager.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

namespace app {
namespace {

std::filesystem::path settings_path() {
    if (const char* local_app_data = std::getenv("LOCALAPPDATA"))
        return std::filesystem::path(local_app_data) / "NeonConnectFour" / "settings.txt";
    return "connect_four_settings.txt";
}

SoundSettings load_sound_settings() {
    SoundSettings settings;
    std::ifstream input(settings_path());
    int muted = 0;
    if (input >> settings.master >> settings.effects >> settings.celebration >> muted) {
        settings.master =
            std::isfinite(settings.master) ? std::clamp(settings.master, 0.0F, 1.0F) : 0.8F;
        settings.effects =
            std::isfinite(settings.effects) ? std::clamp(settings.effects, 0.0F, 1.0F) : 0.9F;
        settings.celebration = std::isfinite(settings.celebration)
                                   ? std::clamp(settings.celebration, 0.0F, 1.0F)
                                   : 0.85F;
        settings.muted = muted != 0;
    }
    return settings;
}

Sound make_tone(float frequency, float duration, bool victory = false) {
    constexpr unsigned rate = 44100;
    std::vector<std::int16_t> samples(static_cast<std::size_t>(rate * duration));
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const float t = static_cast<float>(i) / rate;
        const float envelope = std::pow(1.0F - t / duration, 1.6F);
        const float sweep = victory ? frequency + t * 620.0F : frequency - t * 85.0F;
        const float value = (std::sin(6.2831853F * sweep * t) * 0.72F +
                             std::sin(6.2831853F * sweep * 0.5F * t) * 0.22F) *
                            envelope;
        samples[i] = static_cast<std::int16_t>(value * 27000);
    }
    Wave wave{static_cast<unsigned>(samples.size()), rate, 16, 1, samples.data()};
    return LoadSoundFromWave(wave);
}

}  // namespace

AudioManager::AudioManager() : settings_(load_sound_settings()) {
    InitAudioDevice();
    ready_ = IsAudioDeviceReady();
    if (!ready_) return;

    drop_sound_ = make_tone(260, .13F);
    win_sound_ = make_tone(390, .7F, true);
    drop_loaded_ = IsSoundValid(drop_sound_);
    win_loaded_ = IsSoundValid(win_sound_);
    apply_settings();
}

AudioManager::~AudioManager() {
    if (drop_loaded_) UnloadSound(drop_sound_);
    if (win_loaded_) UnloadSound(win_sound_);
    if (ready_) CloseAudioDevice();
}

void AudioManager::apply_settings() {
    if (!ready_) return;
    SetMasterVolume(settings_.muted ? 0.0F : settings_.master);
    if (drop_loaded_) SetSoundVolume(drop_sound_, settings_.effects);
    if (win_loaded_) SetSoundVolume(win_sound_, settings_.celebration);
}

void AudioManager::save_settings() const {
    const auto path = settings_path();
    std::error_code error;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::trunc);
    if (output)
        output << settings_.master << ' ' << settings_.effects << ' ' << settings_.celebration
               << ' ' << settings_.muted << '\n';
}

void AudioManager::play_drop() const {
    if (drop_loaded_) PlaySound(drop_sound_);
}

void AudioManager::play_victory() const {
    if (win_loaded_) PlaySound(win_sound_);
}

}  // namespace app
