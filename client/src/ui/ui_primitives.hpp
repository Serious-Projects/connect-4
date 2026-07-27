#pragma once

#include <raylib.h>
#include <string>
#include "app/app_types.hpp"
#include "core/connect_four.hpp"

namespace app::ui {

Color player_color(connect4::Player player);
const char* player_name(connect4::Player player);
int reaction_codepoint(const std::string& reaction);
void text(Font font, const char* value, float x, float y, float size, Color color, float spacing = 0);
void draw_disc(float x, float y, float radius, Color color, float glow = 0);
void draw_card(Rectangle bounds, float roundness = .08F, Color fill = surface);
void draw_button(
    Font font,
    Rectangle bounds,
    const char* label,
    Color accent,
    bool enabled = true,
    bool emphasized = false,
    float font_size = 15
);
void draw_input(
    Font font,
    Rectangle bounds,
    const char* value,
    const char* placeholder,
    Color accent,
    bool focused,
    float font_size = 18
);
void draw_status_pill(Font font, Rectangle bounds, const char* label, Color indicator);
int hovered_column(Vector2 mouse);

}  // namespace app::ui
