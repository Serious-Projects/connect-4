#include "ui/font_assets.hpp"

#include <filesystem>

namespace {

std::filesystem::path font_directory() {
    const auto packaged = std::filesystem::path(GetApplicationDirectory()) / "assets" / "fonts";
    if (std::filesystem::exists(packaged)) return packaged;

    const auto source = std::filesystem::path(GetWorkingDirectory()) / "client" / "assets" / "fonts";
    if (std::filesystem::exists(source)) return source;

    return packaged;
}

}  // namespace

namespace app {

FontAssets::FontAssets() {
    const auto fonts = font_directory();
    const auto regular_path = (fonts / "AtkinsonHyperlegible-Regular.ttf").string();
    const auto bold_path = (fonts / "AtkinsonHyperlegible-Bold.ttf").string();
    const auto emoji_path = (fonts / "NotoEmoji.ttf").string();

    // Keep source atlases near their UI sizes so small text remains crisp.
    regular = LoadFontEx(regular_path.c_str(), 24, nullptr, 0);
    bold = LoadFontEx(bold_path.c_str(), 32, nullptr, 0);
    int emoji_codepoints[]{0x1F44D, 0x1F525, 0x1F602, 0x1F62E};
    emoji = LoadFontEx(emoji_path.c_str(), 64, emoji_codepoints, 4);

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

    if (!regular_loaded_ || !bold_loaded_)
        TraceLog(LOG_WARNING, "Bundled interface fonts could not be loaded from %s", fonts.string().c_str());
    if (!emoji_available) TraceLog(LOG_WARNING, "Bundled emoji glyphs are unavailable; using text reaction labels");

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
