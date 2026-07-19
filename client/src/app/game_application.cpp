#include "app/game_application.hpp"
#include <raylib.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <string>
#include <utility>
#include <vector>
#include "app/app_types.hpp"
#include "audio/audio_manager.hpp"
#include "core/connect_four.hpp"
#include "network/online_client.hpp"
#include "network/online_helpers.hpp"
#include "storage/session_store.hpp"
#include "ui/font_assets.hpp"
#include "ui/overlays.hpp"
#include "ui/ui_primitives.hpp"

using connect4::Game;
using connect4::Move;
using connect4::Player;
using connect4::online_helpers::build_replay_model;
using connect4::online_helpers::extract_room_code;
using connect4::online_helpers::room_code_character;
using connect4::online_helpers::sanitize_player_name;
using namespace app;
using namespace app::overlays;
using namespace app::ui;

struct OnlineAttempt {
    bool connected{};
    std::string message;
};

static std::string friendly_online_error(const std::string& error) {
    if (error == "not_your_turn") return "It is not your turn yet.";
    if (error == "column_full") return "That column is full.";
    if (error == "round_over") return "The round is already over.";
    if (error == "spectators_cannot_move") return "Spectators cannot place discs.";
    if (error == "spectators_cannot_vote") return "Spectators cannot vote for a rematch.";
    if (error == "round_not_over") return "Finish this round before requesting a rematch.";
    if (error == "invalid_column") return "That move is outside the board.";
    if (error == "waiting_for_opponent") return "Waiting for both players to connect.";
    if (error == "room_connection_limit") return "That room already has too many viewers.";
    if (error == "reaction_cooldown") return "Reactions need a tiny cooldown.";
    return error.empty() ? "The relay rejected that action." : "Relay error: " + error;
}

int app::run_game() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(screen_width, screen_height, "CONNECT FOUR // NEON DUEL");
    SetTargetFPS(120);
    AudioManager audio;
    SoundSettings& sound_settings = audio.settings();
    FontAssets fonts;
    Font& regular = fonts.regular;
    Font& bold = fonts.bold;
    Font& emoji = fonts.emoji;
    const bool emoji_loaded = fonts.emoji_available;

    Game game;
    FallingDisc falling;
    std::deque<Move> pending_falls;
    std::vector<Confetti> confetti;
    int selected_column = 3;
    int coral_score = 0, gold_score = 0, draws = 0;
    bool outcome_announced = false;
    float screen_flash = 0;
    float win_line_progress = 0;
    online::Client online_client;
    std::future<OnlineAttempt> online_attempt;
    bool online_attempt_pending = false;
    bool attempt_is_reconnect = false;
    bool online_mode = false;
    bool online_lobby = false;
    int online_seat = 0;
    std::string online_room;
    std::string online_token;
    std::string room_input;
    SavedSession saved_session = load_session();
    std::string player_name_input = saved_session.name;
    std::array<std::string, 2> online_names{"Player one", "Player two"};
    bool name_field_active = true;
    std::string online_status;
    float reconnect_timer = -1.0F;
    int opponent_column = -1;
    int opponent_cursor_seat = 0;
    bool rival_connected = false;
    bool move_pending = false;
    int last_cursor_turn = 0;
    int last_cursor_column = -1;
    int latency_ms = -1;
    bool sound_panel = false;
    bool sound_settings_dirty = false;
    bool reaction_panel = false;
    std::vector<ReactionBubble> reactions;
    std::vector<online::ReplayMove> available_replay;
    std::vector<online::ReplayMove> local_replay;
    std::vector<online::ReplayMove> replay_source;
    connect4::online_helpers::ReplayModel replay_model;
    std::array<Player, connect4::rows * connect4::columns> replay_board{};
    bool replay_active = false;
    bool replay_paused = false;
    std::size_t replay_index = 0;
    float replay_delay = 0;
    float replay_speed = 1.0F;
    bool replay_finish_announced = false;

    auto start_drop = [&](int column) {
        if (falling.active || game.over()) return;
        const auto move = game.drop(column);
        if (!move) return;
        falling = {true, *move, board_y - 62, 0, 0};
        local_replay.push_back({move->row, move->column, static_cast<int>(move->player)});
        selected_column = column;
    };

    auto new_round = [&] {
        game.reset();
        falling = {};
        pending_falls.clear();
        confetti.clear();
        outcome_announced = false;
        screen_flash = 0;
        win_line_progress = 0;
        local_replay.clear();
        replay_source.clear();
        replay_active = false;
        replay_paused = false;
        replay_index = 0;
        replay_delay = 0;
        replay_board.fill(Player::none);
        reactions.clear();
    };

    auto reset_visuals = [&] {
        falling = {};
        pending_falls.clear();
        confetti.clear();
        outcome_announced = false;
        screen_flash = 0;
        win_line_progress = 0;
        opponent_column = -1;
        opponent_cursor_seat = 0;
        last_cursor_turn = 0;
        last_cursor_column = -1;
        replay_active = false;
        replay_paused = false;
        replay_index = 0;
        replay_delay = 0;
        replay_board.fill(Player::none);
        reactions.clear();
    };

    auto sync_online = [&](const online::State& state, const online::Event* move_event = nullptr) {
        game.sync_online(state.board, state.turn, state.winner, state.draw, state.winning_cells);
        online_names = state.names;
        coral_score = state.coral_score;
        gold_score = state.gold_score;
        draws = state.draw_score;

        if ((state.winner != 0 || state.draw) && !state.history.empty())
            available_replay = state.history;
        else if (!state.last_replay.empty())
            available_replay = state.last_replay;

        const bool empty = std::all_of(state.board.begin(), state.board.end(), [](int value) {
            return value == 0;
        });

        if (empty) {
            if (replay_active) {
                // A rematch may begin remotely while this client is watching the
                // previous replay. Reset the live-round reveal without killing
                // the isolated replay animation.
                outcome_announced = false;
                screen_flash = 0;
                opponent_column = -1;
                opponent_cursor_seat = 0;
            } else {
                reset_visuals();
            }
        }

        if (move_event && !replay_active && move_event->row >= 0 && move_event->column >= 0) {
            const Player player = move_event->player == 2 ? Player::gold : Player::coral;
            const Move visual_move{move_event->row, move_event->column, player};
            if (falling.active)
                pending_falls.push_back(visual_move);
            else
                falling = {true, visual_move, board_y - 62, 0, 0};
            opponent_column = -1;
            opponent_cursor_seat = 0;
        }
    };

    auto player_label = [&](Player player) -> std::string {
        if (!online_mode) return player_name(player);
        const int index = player == Player::gold ? 1 : 0;
        return online_names[index] +
               (online_seat != 0 && static_cast<int>(player) == online_seat ? " (You)" : "");
    };

    auto start_replay = [&] {
        const auto& source = online_mode ? available_replay : local_replay;
        const auto model = build_replay_model(source);
        if (source.empty()) {
            online_status = "No completed moves are available to replay.";
            return;
        }
        if (!model.valid) {
            online_status = "Replay data was rejected because it is incomplete or invalid.";
            return;
        }
        replay_source = source;
        replay_model = model;
        replay_active = true;
        replay_paused = false;
        replay_index = 0;
        replay_delay = 0.15F;
        replay_board.fill(Player::none);
        falling = {};
        pending_falls.clear();
        confetti.clear();
        win_line_progress = 0;
        replay_finish_announced = false;
    };

    auto stop_replay = [&] {
        replay_active = false;
        replay_paused = false;
        falling = {};
        pending_falls.clear();
        replay_board.fill(Player::none);
        win_line_progress = game.over() && outcome_announced ? 1.0F : 0.0F;
    };

    auto celebrate = [&] {
        audio.play_victory();
        screen_flash = 1;
        for (int i = 0; i < 180; ++i) {
            const float x = static_cast<float>(GetRandomValue(0, screen_width));
            confetti.push_back({{x, static_cast<float>(GetRandomValue(-180, -10))},
                                {static_cast<float>(GetRandomValue(-90, 90)),
                                 static_cast<float>(GetRandomValue(100, 420))},
                                0,
                                static_cast<float>(GetRandomValue(-500, 500)),
                                static_cast<float>(GetRandomValue(180, 400)) / 100.0F,
                                GetRandomValue(0, 1) ? coral : gold});
        }
    };

    auto begin_online_attempt = [&](std::function<OnlineAttempt()> task) {
        try {
            online_attempt = std::async(std::launch::async, std::move(task));
            online_attempt_pending = true;
            return true;
        } catch (const std::exception&) {
            online_attempt_pending = false;
            attempt_is_reconnect = false;
            online_status = "Could not start the network operation. Please try again.";
            return false;
        }
    };

    while (!WindowShouldClose()) {
        const float dt = std::min(GetFrameTime(), 1.0F / 30.0F);
        const Vector2 mouse = GetMousePosition();
        const int hover = hovered_column(mouse);
        if (hover >= 0) selected_column = hover;

        if (!online_attempt_pending) online_client.keep_alive();

        if (online_attempt_pending &&
            online_attempt.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            OnlineAttempt result;
            try {
                result = online_attempt.get();
            } catch (const std::exception&) {
                result = {false, "The network operation failed safely. Please try again."};
            }
            online_attempt_pending = false;
            if (result.connected) {
                reconnect_timer = -1.0F;
                online_status = result.message;
            } else if (attempt_is_reconnect) {
                reconnect_timer = 3.0F;
                online_status = "Relay unavailable - retrying automatically...";
            } else {
                online_status = result.message;
            }
            attempt_is_reconnect = false;
        }

        for (const auto& event : online_client.poll()) {
            if (event.type == online::Event::Type::welcome) {
                move_pending = false;
                online_seat = event.seat;
                online_token = event.token;
                online_mode = true;
                online_lobby = false;
                online_room = online_client.room();
                saved_session = {online_room,
                                 online_token,
                                 player_name_input.empty() ? "Player" : player_name_input};
                save_session(saved_session);
                rival_connected = event.rival_connected;
                reconnect_timer = -1.0F;
                coral_score = gold_score = draws = 0;
                available_replay.clear();
                latency_ms = -1;
                reset_visuals();
                sync_online(event.state);
                online_status = !online_seat            ? "Room is full - spectating."
                                : event.rival_connected ? "Both players connected. Game on!"
                                                        : "Connected. Waiting for your rival.";
            } else if (event.type == online::Event::Type::move) {
                move_pending = false;
                sync_online(event.state, &event);
            } else if (event.type == online::Event::Type::state) {
                move_pending = false;
                sync_online(event.state);
                if (event.state.rematch_votes > 0 && game.over())
                    online_status =
                        "Rematch votes: " + std::to_string(event.state.rematch_votes) + " of 2.";
            } else if (event.type == online::Event::Type::presence) {
                // Presence packets also carry the newest names and board. This
                // is how the host learns the guest's chosen display name.
                sync_online(event.state);
                rival_connected = event.rival_connected;
                if (!rival_connected) {
                    opponent_column = -1;
                    opponent_cursor_seat = 0;
                }
                online_status = event.rival_connected
                                    ? "Both players connected. Game on!"
                                    : "Your rival disconnected - waiting for them to return.";
            } else if (event.type == online::Event::Type::cursor) {
                if (event.seat != online_seat) {
                    opponent_column = event.column;
                    opponent_cursor_seat = event.seat;
                }
            } else if (event.type == online::Event::Type::reaction) {
                reactions.push_back({event.seat, event.reaction, 2.8F});
            } else if (event.type == online::Event::Type::latency) {
                latency_ms = event.latency_ms;
            } else if (event.type == online::Event::Type::error) {
                move_pending = false;
                online_status = friendly_online_error(event.error);
            } else if (event.type == online::Event::Type::disconnected) {
                move_pending = false;
                online_status = "Connection interrupted - reconnecting...";
                rival_connected = false;
                latency_ms = -1;
                opponent_column = -1;
                opponent_cursor_seat = 0;
                reaction_panel = false;
                if (online_mode && !online_room.empty() && !online_token.empty())
                    reconnect_timer = 1.0F;
            }
        }

        if (online_mode && !online_client.connected() && !online_attempt_pending &&
            reconnect_timer >= 0) {
            reconnect_timer -= dt;
            if (reconnect_timer <= 0) {
                online_status = "Reconnecting to room " + online_room + "...";
                const std::string room = online_room;
                player_name_input = sanitize_player_name(player_name_input);
                const std::string name = player_name_input.empty() ? "Player" : player_name_input;
                const std::string token = online_token;
                attempt_is_reconnect = true;
                begin_online_attempt([&online_client, room, name, token] {
                    const bool connected = online_client.connect(room, name, token);
                    return OnlineAttempt{
                        connected,
                        connected ? "Connection restored." : "Could not reconnect."};
                });
            }
        }

        if (!online_lobby && IsKeyPressed(KEY_O) && !online_attempt_pending) {
            if (replay_active) stop_replay();
            online_client.disconnect();
            online_mode = false;
            online_lobby = true;
            if (sound_panel && sound_settings_dirty) audio.save_settings();
            sound_settings_dirty = false;
            sound_panel = false;
            reaction_panel = false;
            online_seat = 0;
            move_pending = false;
            rival_connected = false;
            latency_ms = -1;
            reconnect_timer = -1.0F;
            online_status = "Enter your name, then create a room or join with a code.";
            name_field_active = true;
            // KEY_O and the text-input queue are populated in the same frame.
            // Consume the opener so it cannot leak into the focused name field.
            while (GetCharPressed() > 0) {}
        }

        if (IsKeyPressed(KEY_F1) && !online_attempt_pending) {
            if (replay_active) stop_replay();
            online_client.disconnect();
            online_mode = false;
            move_pending = false;
            online_lobby = false;
            online_room.clear();
            rival_connected = false;
            reconnect_timer = -1.0F;
            reaction_panel = false;
            if (sound_panel && sound_settings_dirty) audio.save_settings();
            sound_settings_dirty = false;
            sound_panel = false;
            coral_score = gold_score = draws = 0;
            new_round();
        }

        if (!online_lobby && IsKeyPressed(KEY_S)) {
            if (sound_panel && sound_settings_dirty) {
                audio.save_settings();
                sound_settings_dirty = false;
            }
            sound_panel = !sound_panel;
            reaction_panel = false;
        }

        if (!online_lobby && online_mode && rival_connected && online_seat != 0 &&
            IsKeyPressed(KEY_E)) {
            reaction_panel = !reaction_panel;
            sound_panel = false;
        }

        const bool replay_ready = online_mode ? !available_replay.empty()
                                              : outcome_announced && !falling.active &&
                                                    pending_falls.empty() && !local_replay.empty();

        const bool current_round_visually_ready =
            !game.over() || (outcome_announced && !falling.active && pending_falls.empty());

        if (!online_lobby && IsKeyPressed(KEY_P) &&
            (replay_active || (replay_ready && current_round_visually_ready))) {
            if (replay_active)
                stop_replay();
            else
                start_replay();
            sound_panel = false;
            reaction_panel = false;
        }

        if (sound_panel) {
            bool changed = false;
            auto update_slider = [&](Rectangle slider, float& value) {
                const Rectangle hit{slider.x - 8, slider.y - 14, slider.width + 16, 36};
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, hit)) {
                    value = std::clamp((mouse.x - slider.x) / slider.width, 0.0F, 1.0F);
                    changed = true;
                }
            };

            update_slider(master_slider, sound_settings.master);
            update_slider(effects_slider, sound_settings.effects);
            update_slider(celebration_slider, sound_settings.celebration);

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                CheckCollisionPointRec(mouse, mute_button)) {
                sound_settings.muted = !sound_settings.muted;
                changed = true;
            }

            if (changed) {
                audio.apply_settings();
                sound_settings_dirty = true;
            }

            if (sound_settings_dirty && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                audio.save_settings();
                sound_settings_dirty = false;
            }
        }

        if (reaction_panel) {
            const char* ids[]{"thumbs", "fire", "laugh", "wow"};
            int selected_reaction = -1;

            for (int index = 0; index < 4; ++index) {
                const Rectangle button{620.0F + index * 76.0F, 330, 60, 60};
                if (IsKeyPressed(KEY_ONE + index) || (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                                                      CheckCollisionPointRec(mouse, button)))
                    selected_reaction = index;
            }

            if (selected_reaction >= 0) {
                online_client.send_reaction(ids[selected_reaction]);
                reaction_panel = false;
            }
        }

        if (replay_active) {
            if (IsKeyPressed(KEY_SPACE)) replay_paused = !replay_paused;
            if (IsKeyPressed(KEY_LEFT_BRACKET)) replay_speed = std::max(0.5F, replay_speed - 0.5F);
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) replay_speed = std::min(3.0F, replay_speed + 0.5F);
            if (IsKeyPressed(KEY_HOME)) start_replay();
            if (!replay_paused && !falling.active && replay_index < replay_source.size()) {
                replay_delay -= dt * replay_speed;
                if (replay_delay <= 0) {
                    const auto& move = replay_source[replay_index];
                    const Player player = move.player == 2 ? Player::gold : Player::coral;
                    falling = {true, {move.row, move.column, player}, board_y - 62, 0, 0};
                }
            }
        }

        const Rectangle room_code_card{160, 190, 233, 40};
        if (online_mode && !online_lobby && !online_room.empty() &&
            (IsKeyPressed(KEY_C) || (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                                     CheckCollisionPointRec(mouse, room_code_card)))) {
            SetClipboardText(online_room.c_str());
            online_status = "Room code copied - paste it to your friend.";
        }

        if (online_lobby) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (CheckCollisionPointRec(mouse, lobby_name_field))
                    name_field_active = true;
                else if (CheckCollisionPointRec(mouse, lobby_code_field))
                    name_field_active = false;
            }

            if (IsKeyPressed(KEY_TAB)) name_field_active = !name_field_active;

            const bool control = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
            if (!name_field_active && control && IsKeyPressed(KEY_V)) {
                if (const char* clipboard = GetClipboardText()) {
                    room_input = extract_room_code(clipboard);
                    if (room_input.empty())
                        online_status =
                            "No valid six-character room code was found in the clipboard.";
                }
            }
            std::string& active_input = name_field_active ? player_name_input : room_input;
            if (IsKeyPressed(KEY_BACKSPACE) && !active_input.empty()) active_input.pop_back();
            if (!control) {
                int character = GetCharPressed();
                while (character > 0) {
                    const bool valid_name =
                        character >= 32 && character <= 126 &&
                        (std::isalnum(static_cast<unsigned char>(character)) || character == ' ' ||
                         character == '-' || character == '_');
                    if (name_field_active && valid_name && active_input.size() < 16)
                        active_input += static_cast<char>(character);
                    else if (!name_field_active &&
                             room_code_character(static_cast<unsigned char>(character)) &&
                             active_input.size() < 6)
                        active_input +=
                            static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
                    character = GetCharPressed();
                }
            }

            const bool create_requested =
                IsKeyPressed(KEY_F2) || (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                                         CheckCollisionPointRec(mouse, lobby_create_button));
            if (create_requested && !online_attempt_pending) {
                online_status = "Creating your private room...";
                player_name_input = sanitize_player_name(player_name_input);
                const std::string name = player_name_input.empty() ? "Player" : player_name_input;
                begin_online_attempt([&online_client, name] {
                    const auto room = online_client.create_room();
                    if (!room)
                        return OnlineAttempt{false,
                                             "Could not reach the relay. Try again in a moment."};
                    if (!online_client.connect(*room, name))
                        return OnlineAttempt{false,
                                             "Room created, but its WebSocket could not connect."};
                    return OnlineAttempt{true, "Joining room " + *room + "..."};
                });
            }
            const bool join_requested = (!name_field_active && IsKeyPressed(KEY_ENTER)) ||
                                        (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                                         CheckCollisionPointRec(mouse, lobby_join_button));
            if (join_requested && room_input.size() == 6 && !online_attempt_pending) {
                online_status = "Joining room " + room_input + "...";
                const std::string room = room_input;
                player_name_input = sanitize_player_name(player_name_input);
                const std::string name = player_name_input.empty() ? "Player" : player_name_input;
                const std::string token =
                    room == online_room && !online_token.empty()               ? online_token
                    : room == saved_session.room && name == saved_session.name ? saved_session.token
                                                                               : "";
                begin_online_attempt([&online_client, room, name, token] {
                    const bool connected = online_client.connect(room, name, token);
                    return OnlineAttempt{connected,
                                         connected
                                             ? "Joining room " + room + "..."
                                             : "Room not found or unavailable. Check the code."};
                });
            } else if (join_requested && room_input.size() != 6) {
                online_status = "A room code has exactly six characters.";
            }
        } else if (!sound_panel && !reaction_panel && !replay_active && !falling.active &&
                   !game.over()) {
            if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT))
                selected_column = std::max(0, selected_column - 1);
            if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT))
                selected_column = std::min(6, selected_column + 1);
            auto play_column = [&](int column) {
                if (online_mode) {
                    if (!rival_connected)
                        online_status = "Waiting for both players to connect.";
                    else if (online_seat == static_cast<int>(game.current()) && !move_pending) {
                        online_client.send_move(column);
                        move_pending = true;
                    } else if (move_pending)
                        online_status = "Move sent - waiting for the relay.";
                    else
                        online_status = online_seat ? "Waiting for the other player."
                                                    : "Spectators cannot place discs.";
                } else
                    start_drop(column);
            };
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) play_column(selected_column);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hover >= 0) play_column(hover);
            for (int key = KEY_ONE; key <= KEY_SEVEN; ++key)
                if (IsKeyPressed(key)) play_column(key - KEY_ONE);
            if (!online_mode && IsKeyPressed(KEY_U) && game.undo() && !local_replay.empty())
                local_replay.pop_back();
        }
        if (online_mode && rival_connected && !online_lobby && !falling.active && !game.over() &&
            online_seat == static_cast<int>(game.current()) &&
            (selected_column != last_cursor_column || last_cursor_turn != online_seat)) {
            online_client.send_cursor(selected_column);
            last_cursor_column = selected_column;
            last_cursor_turn = online_seat;
        } else if (online_mode && online_seat != static_cast<int>(game.current())) {
            last_cursor_turn = 0;
        }
        if (!replay_active && IsKeyPressed(KEY_R)) {
            if (online_mode && game.over() && outcome_announced && !falling.active &&
                pending_falls.empty()) {
                online_client.send_rematch();
                online_status = "Rematch vote sent - waiting for your rival.";
            } else if (online_mode && game.over())
                online_status = "Let the final disc land before voting for a rematch.";
            else if (!online_mode)
                new_round();
        }
        if (IsKeyPressed(KEY_M) && !online_mode && !sound_panel) {
            coral_score = gold_score = draws = 0;
            new_round();
        }

        if (falling.active && (!replay_active || !replay_paused)) {
            const float motion_dt = replay_active ? dt * replay_speed : dt;
            falling.velocity += 2300 * motion_dt;
            falling.y += falling.velocity * motion_dt;
            const float target = board_y + falling.move.row * cell + cell / 2;
            if (falling.y >= target) {
                falling.y = target;
                if (falling.bounces == 0 && falling.velocity > 260) {
                    falling.velocity *= -0.19F;
                    ++falling.bounces;
                    audio.play_drop();
                } else {
                    falling.active = false;
                    falling.velocity = 0;
                    if (replay_active) {
                        replay_board[falling.move.row * connect4::columns + falling.move.column] =
                            falling.move.player;
                        ++replay_index;
                        replay_delay = 0.2F;
                    } else if (!pending_falls.empty()) {
                        const Move next = pending_falls.front();
                        pending_falls.pop_front();
                        falling = {true, next, board_y - 62, 0, 0};
                    }
                }
            }
        }

        if (game.over() && !falling.active && pending_falls.empty() && !outcome_announced) {
            outcome_announced = true;
            win_line_progress = 0;
            if (!online_mode) {
                if (game.winner() == Player::coral)
                    ++coral_score;
                else if (game.winner() == Player::gold)
                    ++gold_score;
                else
                    ++draws;
            }
            if (game.winner() != Player::none) {
                celebrate();
            }
        }
        const bool replay_complete =
            replay_active && replay_index >= replay_source.size() && !falling.active;
        if (replay_complete && !replay_finish_announced) {
            replay_finish_announced = true;
            win_line_progress = 0;
            if (replay_model.winner != 0) celebrate();
        }
        if ((!replay_active && outcome_announced && game.winner() != Player::none) ||
            (replay_complete && replay_model.winner != 0))
            win_line_progress = std::min(1.0F, win_line_progress + dt / 0.72F);
        else
            win_line_progress = 0;
        screen_flash *= .9F;
        for (auto& piece : confetti) {
            piece.life -= dt;
            piece.velocity.y += 240 * dt;
            piece.position.x += piece.velocity.x * dt;
            piece.position.y += piece.velocity.y * dt;
            piece.rotation += piece.spin * dt;
        }
        std::erase_if(confetti, [](const Confetti& piece) {
            return piece.life <= 0 || piece.position.y > screen_height + 20;
        });
        for (auto& reaction : reactions) reaction.life -= dt;
        std::erase_if(reactions, [](const ReactionBubble& reaction) { return reaction.life <= 0; });

        BeginDrawing();
        const bool round_revealed =
            game.over() && !falling.active && pending_falls.empty() && outcome_announced;
        DrawRectangleGradientV(0,
                               0,
                               screen_width,
                               screen_height,
                               Color{13, 18, 33, 255},
                               background);
        DrawCircleGradient({1040, 70}, 330, Color{68, 79, 180, 48}, Color{8, 11, 21, 0});
        DrawCircleGradient({40, 720}, 260, Color{255, 82, 116, 24}, Color{8, 11, 21, 0});
        for (int x = 0; x < screen_width; x += 48)
            DrawLine(x, 0, x, screen_height, Color{255, 255, 255, 5});
        for (int y = 0; y < screen_height; y += 48)
            DrawLine(0, y, screen_width, y, Color{255, 255, 255, 5});

        text(bold, "Connect Four", 40, 34, 42, RAYWHITE, -1);
        text(regular,
             online_mode ? "A private room with a real rival" : "A local strategy duel",
             43,
             84,
             18,
             muted);
        DrawRectangleRounded({955, 44, 178, 38}, .5F, 16, Color{255, 255, 255, 12});
        const char* role_badge = !online_mode       ? "LOCAL 2P"
                                 : online_seat == 1 ? "HOST"
                                 : online_seat == 2 ? "GUEST"
                                                    : "WATCHING";
        const bool relay_live = !online_mode || online_client.connected();
        const std::string mode_badge =
            online_mode && !relay_live ? std::string(role_badge) + "  RETRY"
            : online_mode && latency_ms >= 0
                ? std::string(role_badge) + "  " + std::to_string(latency_ms) + "ms"
                : role_badge;
        const Color quality = !relay_live ? coral
                              : !online_mode || latency_ms < 0 || latency_ms < 90
                                  ? Color{84, 220, 158, 255}
                              : latency_ms < 180 ? gold
                                                 : coral;
        DrawCircle(976, 63, 5, quality);
        text(bold, mode_badge.c_str(), 992, 53, 14, Color{210, 218, 236, 255}, .25F);

        DrawRectangleRounded({45, 142, 382, 488}, .055F, 18, Color{0, 0, 0, 55});
        DrawRectangleRounded({38, 135, 382, 488}, .055F, 18, panel);
        DrawRectangleRoundedLinesEx({38, 135, 382, 488}, .055F, 18, 1, Color{255, 255, 255, 24});
        text(bold, "Match", 68, 164, 25, RAYWHITE);
        if (online_mode) {
            text(bold, "ROOM", 68, 197, 13, muted, .8F);
            text(regular, "CODE", 68, 213, 12, muted, .8F);
            DrawRectangleRounded(room_code_card, .28F, 16, Color{255, 196, 61, 25});
            DrawRectangleRoundedLinesEx(room_code_card, .28F, 16, 1, Color{255, 196, 61, 105});
            text(bold, online_room.c_str(), 175, 197, 23, RAYWHITE, 1.6F);
            DrawLine(326, 198, 326, 222, Color{255, 255, 255, 28});
            text(bold, "C COPY", 337, 203, 11, gold, .4F);
        } else {
            text(regular, "First to your own finish line", 68, 197, 15, muted);
        }

        DrawRectangleRounded({65, 237, 328, 82}, .18F, 16, Color{255, 255, 255, 9});
        draw_disc(96, 278, 19, coral, 1);
        const std::string coral_label = player_label(Player::coral);
        text(bold, coral_label.c_str(), 130, 254, coral_label.size() > 15 ? 16 : 19, RAYWHITE);
        text(regular, online_mode ? "Coral  |  HOST" : "Coral", 130, 281, 14, muted);
        text(bold, TextFormat("%02d", coral_score), 335, 250, 30, coral);

        DrawRectangleRounded({65, 331, 328, 82}, .18F, 16, Color{255, 255, 255, 9});
        draw_disc(96, 372, 19, gold, 1);
        const std::string gold_label = player_label(Player::gold);
        text(bold, gold_label.c_str(), 130, 348, gold_label.size() > 15 ? 16 : 19, RAYWHITE);
        text(regular, online_mode ? "Gold  |  GUEST" : "Gold", 130, 375, 14, muted);
        text(bold, TextFormat("%02d", gold_score), 335, 344, 30, gold);
        text(regular, TextFormat("Draws  %02d", draws), 67, 432, 15, muted);
        DrawLine(65, 468, 393, 468, Color{255, 255, 255, 18});

        const Player focus_player =
            round_revealed && game.winner() != Player::none ? game.winner() : game.current();
        text(regular,
             round_revealed ? (game.draw() ? "Round complete" : "Round winner") : "Current turn",
             68,
             490,
             15,
             muted);
        if (!round_revealed || !game.draw()) {
            draw_disc(93, 545, 23, player_color(focus_player), 2);
            const std::string focus_label = player_label(focus_player);
            text(bold,
                 focus_label.c_str(),
                 132,
                 528,
                 focus_label.size() > 15 ? 17 : 22,
                 player_color(focus_player));
        } else {
            text(bold, "Even match", 68, 529, 22, RAYWHITE);
        }
        DrawRectangleRounded({65, 580, 116, 29}, .45F, 16, Color{255, 255, 255, 10});
        text(bold,
             online_mode ? "R  Vote rematch" : "R  Rematch",
             80,
             586,
             13,
             Color{209, 219, 240, 255});
        DrawRectangleRounded({190, 580, 95, 29}, .45F, 16, Color{255, 255, 255, 10});
        text(bold, online_mode ? "Live relay" : "U  Undo", 208, 586, 13, Color{209, 219, 240, 255});
        DrawRectangleRounded({294, 580, 99, 29}, .45F, 16, Color{255, 255, 255, 10});
        text(bold, online_mode ? "F1  Local" : "M  Reset", 307, 586, 13, Color{209, 219, 240, 255});

        DrawRectangleRounded({board_x - 7, board_y + 2, board_width + 24, board_height + 25},
                             .04F,
                             16,
                             Color{0, 0, 0, 90});
        DrawRectangleRounded({board_x - 12, board_y - 12, board_width + 24, board_height + 24},
                             .04F,
                             16,
                             board_color);
        DrawRectangleRoundedLinesEx(
            {board_x - 12, board_y - 12, board_width + 24, board_height + 24},
            .04F,
            16,
            1,
            Color{255, 255, 255, 36});
        const bool can_aim =
            !replay_active &&
            (!online_mode || (rival_connected && online_seat == static_cast<int>(game.current())));
        const bool show_opponent_aim = online_mode && opponent_column >= 0 &&
                                       opponent_cursor_seat != online_seat &&
                                       opponent_cursor_seat == static_cast<int>(game.current());
        const int aimed_column = show_opponent_aim ? opponent_column : selected_column;
        if (!falling.active && !game.over() && (can_aim || show_opponent_aim)) {
            const Color aim_color = player_color(game.current());
            DrawRectangleRounded(
                {board_x + aimed_column * cell + 7, board_y - 4, cell - 14, board_height + 8},
                .18F,
                12,
                Color{aim_color.r,
                      aim_color.g,
                      aim_color.b,
                      static_cast<unsigned char>(show_opponent_aim ? 22 : 13)});
        }
        for (int row = 0; row < connect4::rows; ++row)
            for (int col = 0; col < connect4::columns; ++col) {
                const float x = board_x + col * cell + cell / 2;
                const float y = board_y + row * cell + cell / 2;
                DrawCircle(static_cast<int>(x + 2),
                           static_cast<int>(y + 4),
                           33,
                           Color{0, 0, 0, 105});
                DrawCircle(static_cast<int>(x), static_cast<int>(y), 31, empty_hole);
                DrawCircleLines(static_cast<int>(x),
                                static_cast<int>(y),
                                31,
                                Color{255, 255, 255, 15});
                const Player player = replay_active ? replay_board[row * connect4::columns + col]
                                                    : game.grid()[row][col];
                const bool is_falling_target =
                    falling.active && falling.move.row == row && falling.move.column == col;
                const bool is_queued_target =
                    std::any_of(pending_falls.begin(), pending_falls.end(), [&](const Move& move) {
                        return move.row == row && move.column == col;
                    });
                if (player != Player::none && !is_falling_target && !is_queued_target)
                    draw_disc(x, y, 29, player_color(player));
            }
        if (falling.active)
            draw_disc(board_x + falling.move.column * cell + cell / 2,
                      falling.y,
                      29,
                      player_color(falling.move.player),
                      2);
        if (!falling.active && !game.over() && can_aim) {
            const float preview_x = board_x + selected_column * cell + cell / 2;
            draw_disc(preview_x, board_y - 55, 22, player_color(game.current()), 2);
            DrawTriangle({preview_x - 6, board_y - 22},
                         {preview_x + 6, board_y - 22},
                         {preview_x, board_y - 13},
                         player_color(game.current()));
        } else if (!falling.active && !game.over() && show_opponent_aim) {
            const float preview_x = board_x + opponent_column * cell + cell / 2;
            const Color hint = player_color(game.current());
            DrawCircle(static_cast<int>(preview_x),
                       static_cast<int>(board_y - 55),
                       22,
                       Color{hint.r, hint.g, hint.b, 42});
            DrawCircleLines(static_cast<int>(preview_x),
                            static_cast<int>(board_y - 55),
                            22,
                            Color{hint.r, hint.g, hint.b, 190});
            DrawTriangle({preview_x - 6, board_y - 22},
                         {preview_x + 6, board_y - 22},
                         {preview_x, board_y - 13},
                         Color{hint.r, hint.g, hint.b, 145});
            const std::string aiming =
                online_names[opponent_cursor_seat == 2 ? 1 : 0] + " is aiming";
            const Vector2 aiming_size = MeasureTextEx(regular, aiming.c_str(), 13, 0);
            text(regular,
                 aiming.c_str(),
                 preview_x - aiming_size.x / 2,
                 board_y - 91,
                 13,
                 Color{hint.r, hint.g, hint.b, 220});
        }
        const int display_winner =
            replay_active ? replay_model.winner : static_cast<int>(game.winner());
        const bool show_win_line = (!replay_active && round_revealed && display_winner != 0) ||
                                   (replay_complete && display_winner != 0);
        if (show_win_line) {
            const auto& wins = replay_active ? replay_model.winning_cells : game.winning_cells();
            const Vector2 start{board_x + wins[0][1] * cell + cell / 2,
                                board_y + wins[0][0] * cell + cell / 2};
            const Vector2 target{board_x + wins[3][1] * cell + cell / 2,
                                 board_y + wins[3][0] * cell + cell / 2};
            const float eased = win_line_progress * win_line_progress * (3 - 2 * win_line_progress);
            const Vector2 end{start.x + (target.x - start.x) * eased,
                              start.y + (target.y - start.y) * eased};
            const Color line_color = player_color(static_cast<Player>(display_winner));
            const float pulse = 17 + std::sin(static_cast<float>(GetTime()) * 7) * 3;
            DrawLineEx(start, end, pulse, Color{line_color.r, line_color.g, line_color.b, 80});
            DrawLineEx(start, end, 7, RAYWHITE);
            DrawCircleV(start, 7, RAYWHITE);
            DrawCircleV(end, 7, RAYWHITE);
            if (win_line_progress >= 1) {
                const float sweep = std::fmod(static_cast<float>(GetTime()) * .62F, 1.0F);
                const Vector2 shine{start.x + (target.x - start.x) * sweep,
                                    start.y + (target.y - start.y) * sweep};
                DrawCircleV(shine, 12, Color{line_color.r, line_color.g, line_color.b, 65});
                DrawCircleV(shine, 4, RAYWHITE);
            }
        }
        for (const auto& piece : confetti) {
            const Rectangle rectangle{piece.position.x, piece.position.y, 9, 16};
            DrawRectanglePro(rectangle, {4.5F, 8}, piece.rotation, piece.color);
        }
        if (round_revealed && !replay_active) {
            std::string banner;
            if (game.draw())
                banner = "It's a draw";
            else if (online_mode) {
                const int winner_index = game.winner() == Player::gold ? 1 : 0;
                banner = online_seat != 0 && static_cast<int>(game.winner()) == online_seat
                             ? "You win"
                             : online_names[winner_index] + " wins";
            } else
                banner = std::string(player_name(game.winner())) + " wins";
            const Vector2 bounds = MeasureTextEx(bold, banner.c_str(), 29, 0);
            const float center = board_x + board_width / 2;
            DrawRectangleRounded({center - bounds.x / 2 - 23, 78, bounds.x + 46, 48},
                                 .45F,
                                 16,
                                 Color{18, 24, 39, 248});
            DrawRectangleRoundedLinesEx({center - bounds.x / 2 - 23, 78, bounds.x + 46, 48},
                                        .45F,
                                        16,
                                        1,
                                        Color{255, 255, 255, 32});
            text(bold,
                 banner.c_str(),
                 center - bounds.x / 2,
                 85,
                 29,
                 game.draw() ? RAYWHITE : player_color(game.winner()));
        } else if (replay_active) {
            const std::string replay_title = "Replay  " + std::to_string(replay_index) + " / " +
                                             std::to_string(replay_source.size());
            const Vector2 bounds = MeasureTextEx(bold, replay_title.c_str(), 25, 0);
            const float center = board_x + board_width / 2;
            DrawRectangleRounded({center - bounds.x / 2 - 20, 78, bounds.x + 40, 45},
                                 .45F,
                                 16,
                                 Color{18, 24, 39, 248});
            text(bold, replay_title.c_str(), center - bounds.x / 2, 87, 25, RAYWHITE);
        }
        text(regular,
             replay_active ? "Space pause  |  [ ] speed  |  Home restart  |  P exit replay"
             : online_mode ? "E reactions  |  S sound  |  P replay after a round"
                           : "Click a column  /  A D + Enter  /  1-7 quick drop  |  S sound",
             568,
             694,
             15,
             muted);
        text(regular, "Esc to exit", 1050, 694, 15, muted);
        text(regular,
             online_mode ? online_status.c_str() : "O  Play online",
             42,
             661,
             15,
             online_mode ? Color{147, 207, 255, 255} : muted);
        if (online_lobby) {
            draw_online_lobby(regular,
                              bold,
                              {player_name_input, room_input, online_status, name_field_active});
        }
        draw_reaction_bubbles(bold, emoji, emoji_loaded, reactions);
        if (reaction_panel) {
            draw_reaction_panel(regular, bold, emoji, emoji_loaded);
        }
        if (sound_panel) {
            draw_sound_panel(regular, bold, sound_settings);
        }
        if (screen_flash > .02F)
            DrawRectangle(0,
                          0,
                          screen_width,
                          screen_height,
                          Color{255, 255, 255, static_cast<unsigned char>(screen_flash * 70)});
        EndDrawing();
    }

    if (sound_settings_dirty) audio.save_settings();

    fonts.release();
    CloseWindow();

    return 0;
}
