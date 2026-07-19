#pragma once

#include <raylib.h>

namespace app {

class FontAssets {
   public:
    FontAssets();
    ~FontAssets();

    FontAssets(const FontAssets&) = delete;
    FontAssets& operator=(const FontAssets&) = delete;

    void release();

    Font regular{};
    Font bold{};
    Font emoji{};
    bool emoji_available{};

   private:
    bool regular_loaded_{};
    bool bold_loaded_{};
    bool emoji_loaded_{};
};

}  // namespace app
