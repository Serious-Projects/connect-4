#include <cassert>
#include <iostream>
#include <random>
#include <vector>
#include "core/connect_four.hpp"
#include "network/online_helpers.hpp"

using namespace connect4;

int main() {
    {
        Game game;
        game.drop(0);
        game.drop(0);
        game.drop(1);
        game.drop(1);
        game.drop(2);
        game.drop(2);
        game.drop(3);
        assert(game.winner() == Player::coral);
        const auto& line = game.winning_cells();
        assert(line.front()[0] == rows - 1 && line.front()[1] == 0);
        assert(line.back()[0] == rows - 1 && line.back()[1] == 3);
    }
    {
        Game game;
        game.drop(0);
        game.drop(1);
        game.drop(0);
        game.drop(1);
        game.drop(0);
        game.drop(1);
        game.drop(0);
        assert(game.winner() == Player::coral);
    }
    {
        Game game;
        game.drop(0);
        game.drop(1);
        game.drop(1);
        game.drop(2);
        game.drop(4);
        game.drop(2);
        game.drop(2);
        game.drop(3);
        game.drop(4);
        game.drop(3);
        game.drop(5);
        game.drop(3);
        game.drop(3);
        assert(game.winner() == Player::coral);
    }
    {
        Game game;
        for (int i = 0; i < rows; ++i) assert(game.drop(0).has_value());
        assert(!game.drop(0).has_value());
        assert(!game.drop(-1).has_value());
    }
    {
        Game game;
        game.drop(2);
        assert(game.current() == Player::gold && game.move_count() == 1);
        assert(game.undo());
        assert(game.current() == Player::coral && game.move_count() == 0);
        assert(game.grid()[rows - 1][2] == Player::none);
    }
    {
        using online_helpers::extract_room_code;
        assert(extract_room_code("SU2VC4") == "SU2VC4");
        assert(extract_room_code("Join room SU2VC4 please") == "SU2VC4");
        assert(extract_room_code("invalid code") == "");
        assert(extract_room_code("ROOMSU2VC4") == "");
        using online_helpers::sanitize_player_name;
        assert(sanitize_player_name("  Jay  ") == "Jay");
        assert(sanitize_player_name("J@a#y") == "Jay");
        assert(sanitize_player_name("                ").empty());
        assert(sanitize_player_name("abcdefghijklmnopq") == "abcdefghijklmnop");
        using online_helpers::json_string_value;
        assert(json_string_value(R"({"name":"Jay"})", "name") == "Jay");
        assert(json_string_value(R"({"name":"A \"quoted\" \\ name"})", "name") == "A \"quoted\" \\ name");
        assert(json_string_value(R"({"name":"line\nnext"})", "name") == "line\nnext");
        assert(json_string_value(R"({"name":"caf\u00e9"})", "name") == "caf\xC3\xA9");
        assert(json_string_value(R"({"name":"unterminated})", "name").empty());
    }
    {
        struct ReplayMove {
            int row;
            int column;
            int player;
        };

        const std::vector<ReplayMove> win{{5, 0, 1}, {5, 1, 2}, {4, 0, 1}, {4, 1, 2}, {3, 0, 1}, {3, 1, 2}, {2, 0, 1}};
        const auto replay = online_helpers::build_replay_model(win);
        assert(replay.valid && replay.winner == 1 && !replay.draw);
        const std::array<int, 2> expected_start{2, 0};
        const std::array<int, 2> expected_end{5, 0};
        assert(replay.winning_cells.front() == expected_start);
        assert(replay.winning_cells.back() == expected_end);

        const std::vector<ReplayMove> bad_row{{4, 0, 1}};
        assert(!online_helpers::build_replay_model(bad_row).valid);
        const std::vector<ReplayMove> bad_player{{5, 0, 3}};
        assert(!online_helpers::build_replay_model(bad_player).valid);
        const std::vector<ReplayMove> repeated_turn{{5, 0, 1}, {5, 1, 1}};
        assert(!online_helpers::build_replay_model(repeated_turn).valid);

        const int draw_columns[]{4, 1, 3, 5, 2, 3, 3, 2, 1, 1, 4, 4, 0, 0, 5, 6, 1, 3, 6, 1, 1,
                                 5, 3, 2, 6, 2, 2, 2, 3, 0, 0, 6, 4, 0, 6, 0, 5, 5, 6, 4, 5, 4};
        std::array<int, 7> heights{};
        std::vector<ReplayMove> draw;
        int player = 1;
        for (const int column : draw_columns) {
            draw.push_back({5 - heights[column]++, column, player});
            player = player == 1 ? 2 : 1;
        }
        const auto draw_replay = online_helpers::build_replay_model(draw);
        assert(draw_replay.valid && draw_replay.draw && draw_replay.winner == 0);

        const std::vector<ReplayMove> gold_starts{{5, 2, 2}, {5, 3, 1}, {4, 2, 2}, {4, 3, 1},
                                                  {3, 2, 2}, {3, 3, 1}, {2, 2, 2}};
        const auto gold_replay = online_helpers::build_replay_model(gold_starts);
        assert(gold_replay.valid && gold_replay.winner == 2);
    }
    {
        std::mt19937 random(0xC04F0U);
        for (int trial = 0; trial < 2500; ++trial) {
            Game game;

            struct ReplayMove {
                int row;
                int column;
                int player;
            };

            std::vector<ReplayMove> history;
            while (!game.over()) {
                const int column = static_cast<int>(random() % columns);
                const auto move = game.drop(column);
                if (move) history.push_back({move->row, move->column, static_cast<int>(move->player)});
            }
            const auto replay = online_helpers::build_replay_model(history);
            assert(replay.valid);
            assert(replay.winner == static_cast<int>(game.winner()));
            assert(replay.draw == game.draw());
            for (int row = 0; row < rows; ++row)
                for (int column = 0; column < columns; ++column)
                    assert(replay.board[row * columns + column] == static_cast<int>(game.grid()[row][column]));
        }
    }
    std::cout << "All Connect Four logic tests passed.\n";
}
