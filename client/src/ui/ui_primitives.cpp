#include "ui/ui_primitives.hpp"
#include <algorithm>
#include <cmath>
#include "app/app_types.hpp"

namespace app::ui {

Color player_color(connect4::Player player) {
    return player == connect4::Player::coral ? coral : gold;
}

const char* player_name(connect4::Player player) {
    return player == connect4::Player::coral ? "Player one" : "Player two";
}

int reaction_codepoint(const std::string& reaction) {
    if (reaction == "fire") return 0x1F525;
    if (reaction == "laugh") return 0x1F602;
    if (reaction == "wow") return 0x1F62E;
    return 0x1F44D;
}

void text(Font font, const char* value, float x, float y, float size, Color color, float spacing) {
    DrawTextEx(font, value, {x, y}, size, spacing, color);
}

void draw_disc(float x, float y, float radius, Color color, float glow) {
    for (int i = static_cast<int>(glow); i > 0; --i)
        DrawCircle(
            static_cast<int>(x),
            static_cast<int>(y),
            radius + i * 2,
            Color{color.r, color.g, color.b, static_cast<unsigned char>(8 + i * 7)}
        );
    DrawCircle(static_cast<int>(x + 3), static_cast<int>(y + 6), radius + 1, Color{0, 0, 0, 100});
    const Color edge{
        static_cast<unsigned char>(color.r * .72F),
        static_cast<unsigned char>(color.g * .72F),
        static_cast<unsigned char>(color.b * .72F),
        255
    };
    DrawCircleGradient({x, y}, radius, Color{255, 255, 255, 120}, edge);
    DrawCircle(
        static_cast<int>(x - radius * .28F),
        static_cast<int>(y - radius * .3F),
        radius * .16F,
        Color{255, 255, 255, 145}
    );
    DrawCircleLines(static_cast<int>(x), static_cast<int>(y), radius, Color{255, 255, 255, 90});
}

void draw_card(Rectangle bounds, float roundness, Color fill) {
    DrawRectangleRounded({bounds.x + 4, bounds.y + 7, bounds.width, bounds.height}, roundness, 18, Color{0, 0, 0, 72});
    DrawRectangleRounded(bounds, roundness, 18, fill);
    DrawRectangleRoundedLinesEx(bounds, roundness, 18, 1, border);
}

void draw_button(
    Font font,
    Rectangle bounds,
    const char* label,
    Color accent,
    bool enabled,
    bool emphasized,
    float font_size
) {
    const bool hovered = enabled && CheckCollisionPointRec(GetMousePosition(), bounds);
    const unsigned char fill_alpha = static_cast<unsigned char>(
        !enabled     ? 8
        : emphasized ? (hovered ? 235 : 210)
                     : (hovered ? 50 : 20)
    );
    const Color fill = emphasized ? Color{accent.r, accent.g, accent.b, fill_alpha} : Color{255, 255, 255, fill_alpha};
    const Color outline = !enabled ? Color{255, 255, 255, 16}
                          : hovered
                              ? Color{accent.r, accent.g, accent.b, 180}
                              : Color{accent.r, accent.g, accent.b, static_cast<unsigned char>(emphasized ? 150 : 55)};
    if (emphasized && enabled)
        DrawRectangleRounded(
            {bounds.x + 1, bounds.y + 4, bounds.width, bounds.height},
            .24F,
            14,
            Color{accent.r, accent.g, accent.b, static_cast<unsigned char>(hovered ? 38 : 22)}
        );
    DrawRectangleRounded(bounds, .24F, 14, fill);
    DrawRectangleRoundedLinesEx(bounds, .24F, 14, hovered ? 2 : 1, outline);
    const Vector2 measured = MeasureTextEx(font, label, font_size, 0);
    const Color label_color = !enabled     ? Color{muted.r, muted.g, muted.b, 150}
                              : emphasized ? Color{9, 13, 24, 255}
                                           : text_primary;
    text(
        font,
        label,
        bounds.x + (bounds.width - measured.x) / 2,
        bounds.y + (bounds.height - measured.y) / 2 - 1,
        font_size,
        label_color
    );
}

void draw_input(
    Font font,
    Rectangle bounds,
    const char* value,
    const char* placeholder,
    Color accent,
    bool focused,
    float font_size
) {
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    DrawRectangleRounded(bounds, .18F, 14, Color{8, 12, 23, 205});
    DrawRectangleRoundedLinesEx(
        bounds,
        .18F,
        14,
        focused ? 2 : 1,
        focused   ? Color{accent.r, accent.g, accent.b, 210}
        : hovered ? Color{255, 255, 255, 60}
                  : Color{255, 255, 255, 28}
    );
    const bool empty = !value || value[0] == '\0';
    text(
        font,
        empty ? placeholder : value,
        bounds.x + 18,
        bounds.y + (bounds.height - font_size) / 2 - 1,
        font_size,
        empty ? muted : text_primary,
        empty ? 0 : .4F
    );
    if (focused && !empty && std::fmod(GetTime(), 1.0) < .55)
        DrawRectangle(
            static_cast<int>(bounds.x + 20 + MeasureTextEx(font, value, font_size, .4F).x),
            static_cast<int>(bounds.y + 14),
            2,
            static_cast<int>(bounds.height - 28),
            accent
        );
}

void draw_status_pill(Font font, Rectangle bounds, const char* label, Color indicator) {
    DrawRectangleRounded(bounds, .5F, 16, Color{255, 255, 255, 11});
    DrawRectangleRoundedLinesEx(bounds, .5F, 16, 1, Color{255, 255, 255, 18});
    DrawCircle(static_cast<int>(bounds.x + 20), static_cast<int>(bounds.y + bounds.height / 2), 5, indicator);
    text(font, label, bounds.x + 34, bounds.y + 10, 13, Color{210, 218, 236, 255}, .25F);
}

int hovered_column(Vector2 mouse) {
    if (mouse.x < board_x || mouse.x >= board_x + board_width || mouse.y < board_y - 70 ||
        mouse.y > board_y + board_height)
        return -1;
    return std::clamp(static_cast<int>((mouse.x - board_x) / cell), 0, connect4::columns - 1);
}

}  // namespace app::ui
