#pragma once

#include <raylib.h>
#include <string>
#include "core/connect_four.hpp"

namespace app {

inline constexpr int screen_width = 1180;
inline constexpr int screen_height = 760;
inline constexpr float cell = 84.0F;
inline constexpr float board_x = 510.0F;
inline constexpr float board_y = 145.0F;
inline constexpr float board_width = cell * connect4::columns;
inline constexpr float board_height = cell * connect4::rows;

inline constexpr Color background{8, 11, 21, 255};
inline constexpr Color panel{18, 24, 39, 245};
inline constexpr Color board_color{23, 31, 51, 255};
inline constexpr Color coral{255, 82, 116, 255};
inline constexpr Color gold{255, 196, 61, 255};
inline constexpr Color empty_hole{7, 10, 20, 255};
inline constexpr Color muted{139, 151, 178, 255};

struct FallingDisc {
    bool active{};
    connect4::Move move{};
    float y{};
    float velocity{};
    int bounces{};
};

struct Confetti {
    Vector2 position{};
    Vector2 velocity{};
    float rotation{};
    float spin{};
    float life{};
    Color color{};
};

struct ReactionBubble {
    int seat{};
    std::string reaction;
    float life{2.8F};
};

}  // namespace app
