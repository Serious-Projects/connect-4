#pragma once

#include <raylib.h>

namespace app {

struct SoundSettings {
    float master{0.85F};
    float effects{0.9F};
    float celebration{0.8F};
    bool muted{};
};

class AudioManager {
   public:
    AudioManager();
    ~AudioManager();

    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    bool ready() const {
        return ready_;
    }
    SoundSettings& settings() {
        return settings_;
    }
    const SoundSettings& settings() const {
        return settings_;
    }

    void apply_settings();
    void save_settings() const;
    void play_drop() const;
    void play_victory() const;

   private:
    bool ready_{};
    Sound drop_sound_{};
    Sound win_sound_{};
    bool drop_loaded_{};
    bool win_loaded_{};
    SoundSettings settings_{};
};

}  // namespace app
