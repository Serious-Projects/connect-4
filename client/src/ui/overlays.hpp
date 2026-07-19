#pragma once

#include <raylib.h>
#include <string>
#include <vector>

#include "app/app_types.hpp"
#include "audio/audio_manager.hpp"

namespace app::overlays {

inline constexpr Rectangle lobby_name_field{310, 260, 560, 52};
inline constexpr Rectangle lobby_create_button{310, 350, 235, 58};
inline constexpr Rectangle lobby_code_field{575, 350, 175, 58};
inline constexpr Rectangle lobby_join_button{765, 350, 105, 58};

inline constexpr Rectangle master_slider{460, 284, 280, 8};
inline constexpr Rectangle effects_slider{460, 348, 280, 8};
inline constexpr Rectangle celebration_slider{460, 412, 280, 8};
inline constexpr Rectangle mute_button{460, 462, 132, 42};

struct LobbyView {
    const std::string& player_name;
    const std::string& room_code;
    const std::string& status;
    bool name_field_active{};
};

void draw_online_lobby(Font regular, Font bold, const LobbyView& view);
void draw_reaction_bubbles(Font bold, Font emoji, bool emoji_available, const std::vector<ReactionBubble>& reactions);
void draw_reaction_panel(Font regular, Font bold, Font emoji, bool emoji_available);
void draw_sound_panel(Font regular, Font bold, const SoundSettings& settings);

}  // namespace app::overlays
