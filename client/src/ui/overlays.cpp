#include "ui/overlays.hpp"

#include <algorithm>
#include <cmath>

#include "ui/ui_primitives.hpp"

namespace app::overlays {
namespace {

void draw_slider(Font regular, Font bold, const char* label, Rectangle slider, float value) {
    ui::text(bold, label, 420, slider.y - 24, 15, RAYWHITE);
    DrawRectangleRounded(slider, .8F, 8, Color{255, 255, 255, 20});
    DrawRectangleRounded({slider.x, slider.y, slider.width * value, slider.height}, .8F, 8, coral);
    DrawCircle(static_cast<int>(slider.x + slider.width * value),
               static_cast<int>(slider.y + slider.height / 2),
               8,
               RAYWHITE);
    ui::text(regular,
             TextFormat("%d%%", static_cast<int>(value * 100)),
             750,
             slider.y - 6,
             13,
             muted);
}

}  // namespace

void draw_online_lobby(Font regular, Font bold, const LobbyView& view) {
    DrawRectangle(0, 0, screen_width, screen_height, Color{3, 6, 14, 205});
    DrawRectangleRounded({260, 120, 660, 470}, .045F, 22, panel);
    DrawRectangleRoundedLinesEx({260, 120, 660, 470}, .045F, 22, 1, Color{255, 255, 255, 38});
    ui::text(bold, "Play online", 310, 154, 35, RAYWHITE);
    ui::text(regular, "Private rooms, real names, live opponent intent.", 312, 198, 18, muted);

    ui::text(bold, "YOUR NAME", 310, 235, 13, view.name_field_active ? coral : muted, .8F);
    DrawRectangleRounded(lobby_name_field, .18F, 14, Color{255, 255, 255, 10});
    DrawRectangleRoundedLinesEx(
        lobby_name_field,
        .18F,
        14,
        view.name_field_active ? 2 : 1,
        view.name_field_active ? Color{coral.r, coral.g, coral.b, 170} : Color{255, 255, 255, 28});
    ui::text(bold,
             view.player_name.empty() ? "TYPE YOUR NAME" : view.player_name.c_str(),
             330,
             275,
             20,
             view.player_name.empty() ? muted : RAYWHITE);

    DrawRectangleRounded(lobby_create_button, .2F, 16, Color{coral.r, coral.g, coral.b, 44});
    DrawRectangleRoundedLinesEx(lobby_create_button,
                                .2F,
                                16,
                                1,
                                Color{coral.r, coral.g, coral.b, 110});
    ui::text(bold, "Create room", 352, 368, 19, coral);
    ui::text(bold, "ROOM CODE", 575, 329, 12, !view.name_field_active ? gold : muted, .8F);
    DrawRectangleRounded(lobby_code_field, .18F, 14, Color{255, 255, 255, 10});
    DrawRectangleRoundedLinesEx(
        lobby_code_field,
        .18F,
        14,
        !view.name_field_active ? 2 : 1,
        !view.name_field_active ? Color{gold.r, gold.g, gold.b, 170} : Color{255, 255, 255, 28});
    ui::text(bold,
             view.room_code.empty() ? "PASTE CODE" : view.room_code.c_str(),
             592,
             368,
             18,
             view.room_code.empty() ? muted : gold,
             1.0F);
    DrawRectangleRounded(
        lobby_join_button,
        .22F,
        16,
        view.room_code.size() == 6 ? Color{gold.r, gold.g, gold.b, 45} : Color{255, 255, 255, 8});
    ui::text(bold, "Join", 795, 369, 18, view.room_code.size() == 6 ? gold : muted);

    ui::text(regular, view.status.c_str(), 312, 444, 16, Color{147, 207, 255, 255});
    ui::text(regular,
             "Click the code field and press Ctrl+V to paste  |  Tab switches fields",
             312,
             480,
             14,
             muted);
    ui::text(regular,
             "F2 creates a room  |  Enter joins  |  F1 local play  |  Esc exits",
             312,
             535,
             14,
             muted);
}

void draw_reaction_bubbles(Font bold,
                           Font emoji,
                           bool emoji_available,
                           const std::vector<ReactionBubble>& reactions) {
    for (const auto& reaction : reactions) {
        const float elapsed = 2.8F - reaction.life;
        const float alpha_scale = std::min(1.0F, reaction.life / 0.45F);
        const float x = reaction.seat == 1 ? board_x + 86 : board_x + board_width - 86;
        const float y = board_y + 120 - elapsed * 34;
        const unsigned char alpha = static_cast<unsigned char>(220 * alpha_scale);
        DrawCircleV({x, y}, 33 + std::sin(elapsed * 8) * 2, Color{18, 24, 39, alpha});
        DrawCircleLines(static_cast<int>(x),
                        static_cast<int>(y),
                        33,
                        Color{255, 255, 255, static_cast<unsigned char>(80 * alpha_scale)});
        if (emoji_available) {
            DrawTextCodepoint(emoji,
                              ui::reaction_codepoint(reaction.reaction),
                              {x - 22, y - 23},
                              44,
                              Color{255, 255, 255, alpha});
        } else {
            const std::string fallback = reaction.reaction == "thumbs"  ? "GG"
                                         : reaction.reaction == "fire"  ? "HOT"
                                         : reaction.reaction == "laugh" ? "LOL"
                                                                        : "WOW";
            const Vector2 size = MeasureTextEx(bold, fallback.c_str(), 15, 0);
            ui::text(bold,
                     fallback.c_str(),
                     x - size.x / 2,
                     y - 8,
                     15,
                     Color{255, 255, 255, alpha});
        }
    }
}

void draw_reaction_panel(Font regular, Font bold, Font emoji, bool emoji_available) {
    DrawRectangleRounded({592, 282, 350, 130}, .14F, 18, Color{18, 24, 39, 250});
    DrawRectangleRoundedLinesEx({592, 282, 350, 130}, .14F, 18, 1, Color{255, 255, 255, 36});
    ui::text(bold, "Send a reaction", 616, 297, 17, RAYWHITE);
    const char* labels[]{"GG", "FIRE", "LOL", "WOW"};
    const char* ids[]{"thumbs", "fire", "laugh", "wow"};
    for (int index = 0; index < 4; ++index) {
        const Rectangle button{620.0F + index * 76.0F, 330, 60, 60};
        DrawRectangleRounded(button, .32F, 14, Color{255, 255, 255, 11});
        if (emoji_available)
            DrawTextCodepoint(emoji,
                              ui::reaction_codepoint(ids[index]),
                              {button.x + 12, button.y + 7},
                              36,
                              RAYWHITE);
        else
            ui::text(bold, labels[index], button.x + 10, button.y + 20, 13, RAYWHITE);
        ui::text(regular, TextFormat("%d", index + 1), button.x + 46, button.y + 42, 10, muted);
    }
}

void draw_sound_panel(Font regular, Font bold, const SoundSettings& settings) {
    DrawRectangle(0, 0, screen_width, screen_height, Color{3, 6, 14, 190});
    DrawRectangleRounded({370, 174, 440, 370}, .06F, 20, panel);
    DrawRectangleRoundedLinesEx({370, 174, 440, 370}, .06F, 20, 1, Color{255, 255, 255, 38});
    ui::text(bold, "Sound controls", 420, 210, 30, RAYWHITE);
    ui::text(regular, "Saved automatically", 422, 247, 14, muted);
    draw_slider(regular, bold, "Master", master_slider, settings.master);
    draw_slider(regular, bold, "Disc effects", effects_slider, settings.effects);
    draw_slider(regular, bold, "Victory celebration", celebration_slider, settings.celebration);
    DrawRectangleRounded(
        mute_button,
        .3F,
        14,
        settings.muted ? Color{coral.r, coral.g, coral.b, 55} : Color{255, 255, 255, 12});
    ui::text(bold,
             settings.muted ? "Unmute" : "Mute all",
             487,
             474,
             15,
             settings.muted ? coral : RAYWHITE);
    ui::text(regular, "Press S to close", 625, 476, 14, muted);
}

}  // namespace app::overlays
