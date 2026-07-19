#pragma once

#include <raylib.h>
#include <string>
#include "core/connect_four.hpp"

namespace app::ui {

Color player_color(connect4::Player player);
const char* player_name(connect4::Player player);
int reaction_codepoint(const std::string& reaction);
void text(Font font,
          const char* value,
          float x,
          float y,
          float size,
          Color color,
          float spacing = 0);
void draw_disc(float x, float y, float radius, Color color, float glow = 0);
int hovered_column(Vector2 mouse);

}  // namespace app::ui
