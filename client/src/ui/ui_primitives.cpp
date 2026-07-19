#include "ui/ui_primitives.hpp"
#include <algorithm>
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
        DrawCircle(static_cast<int>(x),
                   static_cast<int>(y),
                   radius + i * 2,
                   Color{color.r, color.g, color.b, static_cast<unsigned char>(8 + i * 7)});
    DrawCircle(static_cast<int>(x + 3), static_cast<int>(y + 6), radius + 1, Color{0, 0, 0, 100});
    const Color edge{static_cast<unsigned char>(color.r * .72F),
                     static_cast<unsigned char>(color.g * .72F),
                     static_cast<unsigned char>(color.b * .72F),
                     255};
    DrawCircleGradient({x, y}, radius, Color{255, 255, 255, 120}, edge);
    DrawCircle(static_cast<int>(x - radius * .28F),
               static_cast<int>(y - radius * .3F),
               radius * .16F,
               Color{255, 255, 255, 145});
    DrawCircleLines(static_cast<int>(x), static_cast<int>(y), radius, Color{255, 255, 255, 90});
}

int hovered_column(Vector2 mouse) {
    if (mouse.x < board_x || mouse.x >= board_x + board_width || mouse.y < board_y - 70 ||
        mouse.y > board_y + board_height)
        return -1;
    return std::clamp(static_cast<int>((mouse.x - board_x) / cell), 0, connect4::columns - 1);
}

}  // namespace app::ui
