#include "ui/font_assets.hpp"

namespace app {

FontAssets::FontAssets() {
    // Keep source atlases near their UI sizes so small text remains crisp.
    regular = LoadFontEx("C:/Windows/Fonts/segoeui.ttf", 24, nullptr, 0);
    bold = LoadFontEx("C:/Windows/Fonts/segoeuib.ttf", 32, nullptr, 0);
    int emoji_codepoints[]{0x1F44D, 0x1F525, 0x1F602, 0x1F62E};
    emoji = LoadFontEx("C:/Windows/Fonts/seguiemj.ttf", 64, emoji_codepoints, 4);

    regular_loaded_ = IsFontValid(regular);
    bold_loaded_ = IsFontValid(bold);
    emoji_loaded_ = IsFontValid(emoji);
    emoji_available = emoji_loaded_;
    if (emoji_loaded_)
        for (const int codepoint : emoji_codepoints)
            if (GetGlyphInfo(emoji, codepoint).value != codepoint) emoji_available = false;

    if (!regular_loaded_) regular = GetFontDefault();
    if (!bold_loaded_) bold = GetFontDefault();
    if (!emoji_loaded_) emoji = GetFontDefault();

    SetTextureFilter(regular.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(bold.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(emoji.texture, TEXTURE_FILTER_BILINEAR);
}

FontAssets::~FontAssets() {
    release();
}

void FontAssets::release() {
    if (regular_loaded_) UnloadFont(regular);
    if (bold_loaded_) UnloadFont(bold);
    if (emoji_loaded_) UnloadFont(emoji);
    regular_loaded_ = false;
    bold_loaded_ = false;
    emoji_loaded_ = false;
}

}  // namespace app
