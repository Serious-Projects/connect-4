#include "ui/overlays.hpp"

#include <algorithm>
#include <cmath>

#include "ui/ui_primitives.hpp"

namespace app::overlays {
namespace {

void draw_slider(Font regular, Font bold, const char* label, Rectangle slider, float value) {
    ui::text(bold, label, 420, slider.y - 25, 15, text_primary);
    DrawRectangleRounded(slider, .8F, 8, Color{255, 255, 255, 20});
    DrawRectangleRounded({slider.x, slider.y, slider.width * value, slider.height}, .8F, 8, coral);
    DrawCircle(
        static_cast<int>(slider.x + slider.width * value),
        static_cast<int>(slider.y + slider.height / 2),
        12,
        Color{coral.r, coral.g, coral.b, 28}
    );
    DrawCircle(
        static_cast<int>(slider.x + slider.width * value),
        static_cast<int>(slider.y + slider.height / 2),
        8,
        RAYWHITE
    );
    ui::text(regular, TextFormat("%d%%", static_cast<int>(value * 100)), 750, slider.y - 7, 13, text_secondary);
}

}  // namespace

void draw_online_lobby(Font regular, Font bold, const LobbyView& view) {
    DrawRectangle(0, 0, screen_width, screen_height, Color{3, 6, 14, 220});
    DrawCircleGradient({880, 100}, 260, Color{65, 79, 180, 32}, Color{8, 11, 21, 0});
    ui::draw_card({260, 100, 660, 510}, .045F, Color{17, 23, 38, 252});
    DrawRectangleRounded({288, 130, 46, 46}, .32F, 14, Color{coral.r, coral.g, coral.b, 32});
    DrawCircle(303, 153, 7, coral);
    DrawCircle(319, 153, 7, gold);
    ui::text(bold, "Play online", 350, 124, 31, text_primary);
    ui::text(regular, "Create a private match or join a friend's room.", 350, 158, 16, text_secondary);
    ui::text(bold, "ESC", 858, 126, 11, muted, .6F);

    ui::text(bold, "DISPLAY NAME", 310, 218, 12, view.name_field_active ? coral : text_secondary, .9F);
    ui::draw_input(
        bold,
        lobby_name_field,
        view.player_name.c_str(),
        "Type your name",
        coral,
        view.name_field_active,
        19
    );

    ui::text(bold, "START A NEW MATCH", 310, 329, 12, text_secondary, .9F);
    ui::draw_button(
        bold,
        lobby_create_button,
        view.busy ? "Connecting..." : "Create private room",
        coral,
        !view.busy,
        true,
        16
    );
    ui::text(bold, "OR JOIN", 575, 329, 12, text_secondary, .9F);
    ui::draw_input(bold, lobby_code_field, view.room_code.c_str(), "ROOM CODE", gold, !view.name_field_active, 17);
    ui::draw_button(bold, lobby_join_button, "Join", gold, !view.busy && view.room_code.size() == 6, true, 16);

    DrawRectangleRounded({310, 436, 560, 48}, .18F, 14, Color{147, 207, 255, 10});
    DrawCircle(330, 460, 5, view.busy ? gold : Color{147, 207, 255, 255});
    ui::text(regular, view.status.c_str(), 346, 449, 15, Color{164, 211, 255, 255});
    DrawLine(310, 510, 870, 510, Color{255, 255, 255, 18});
    ui::text(regular, "Ctrl+V  Paste code", 310, 534, 13, text_secondary);
    ui::text(regular, "Tab  Switch field", 474, 534, 13, text_secondary);
    ui::text(regular, "F1  Local play", 634, 534, 13, text_secondary);
    ui::text(regular, "Enter  Join", 768, 534, 13, text_secondary);
    ui::text(
        regular,
        "Your room code is private. Share it only with the person you want to play.",
        310,
        569,
        13,
        muted
    );
}

void draw_reaction_bubbles(Font bold, Font emoji, bool emoji_available, const std::vector<ReactionBubble>& reactions) {
    for (const auto& reaction : reactions) {
        const float elapsed = 2.8F - reaction.life;
        const float alpha_scale = std::min(1.0F, reaction.life / 0.45F);
        const float x = reaction.seat == 1 ? board_x + 86 : board_x + board_width - 86;
        const float y = board_y + 120 - elapsed * 34;
        const unsigned char alpha = static_cast<unsigned char>(220 * alpha_scale);
        DrawCircleV({x, y}, 33 + std::sin(elapsed * 8) * 2, Color{18, 24, 39, alpha});
        DrawCircleLines(
            static_cast<int>(x),
            static_cast<int>(y),
            33,
            Color{255, 255, 255, static_cast<unsigned char>(80 * alpha_scale)}
        );
        if (emoji_available) {
            DrawTextCodepoint(
                emoji,
                ui::reaction_codepoint(reaction.reaction),
                {x - 22, y - 23},
                44,
                Color{255, 255, 255, alpha}
            );
        } else {
            const std::string fallback = reaction.reaction == "thumbs"  ? "GG"
                                         : reaction.reaction == "fire"  ? "HOT"
                                         : reaction.reaction == "laugh" ? "LOL"
                                                                        : "WOW";
            const Vector2 size = MeasureTextEx(bold, fallback.c_str(), 15, 0);
            ui::text(bold, fallback.c_str(), x - size.x / 2, y - 8, 15, Color{255, 255, 255, alpha});
        }
    }
}

void draw_reaction_panel(Font regular, Font bold, Font emoji, bool emoji_available) {
    ui::draw_card({592, 282, 350, 130}, .14F, surface_raised);
    ui::text(bold, "Send a reaction", 616, 297, 17, text_primary);
    const char* labels[]{"GG", "FIRE", "LOL", "WOW"};
    const char* ids[]{"thumbs", "fire", "laugh", "wow"};
    for (int index = 0; index < 4; ++index) {
        const Rectangle button{620.0F + index * 76.0F, 330, 60, 60};
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), button);
        DrawRectangleRounded(button, .32F, 14, Color{255, 255, 255, static_cast<unsigned char>(hovered ? 28 : 11)});
        DrawRectangleRoundedLinesEx(
            button,
            .32F,
            14,
            hovered ? 2 : 1,
            hovered ? Color{coral.r, coral.g, coral.b, 150} : Color{255, 255, 255, 20}
        );
        if (emoji_available)
            DrawTextCodepoint(emoji, ui::reaction_codepoint(ids[index]), {button.x + 12, button.y + 7}, 36, RAYWHITE);
        else
            ui::text(bold, labels[index], button.x + 10, button.y + 20, 13, RAYWHITE);
        ui::text(regular, TextFormat("%d", index + 1), button.x + 46, button.y + 42, 10, muted);
    }
}

void draw_sound_panel(Font regular, Font bold, const SoundSettings& settings) {
    DrawRectangle(0, 0, screen_width, screen_height, Color{3, 6, 14, 215});
    ui::draw_card({370, 174, 440, 370}, .06F, Color{17, 23, 38, 252});
    ui::text(bold, "Sound", 420, 207, 30, text_primary);
    ui::text(regular, "Changes are saved automatically", 422, 244, 14, text_secondary);
    ui::text(bold, "ESC", 752, 214, 11, muted, .6F);
    draw_slider(regular, bold, "Master", master_slider, settings.master);
    draw_slider(regular, bold, "Disc effects", effects_slider, settings.effects);
    draw_slider(regular, bold, "Victory celebration", celebration_slider, settings.celebration);
    ui::draw_button(bold, mute_button, settings.muted ? "Unmute" : "Mute all", coral, true, settings.muted, 15);
    ui::text(regular, "S or Esc to close", 625, 476, 14, muted);
}

}  // namespace app::overlays
