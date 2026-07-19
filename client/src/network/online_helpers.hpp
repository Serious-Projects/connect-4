#pragma once

#include <array>
#include <cctype>
#include <string>

namespace connect4::online_helpers {

inline bool room_code_character(unsigned char character) {
    character = static_cast<unsigned char>(std::toupper(character));
    return (character >= 'A' && character <= 'Z' && character != 'I' && character != 'O') ||
           (character >= '2' && character <= '9');
}

inline std::string sanitize_player_name(const std::string& input) {
    std::string result;
    for (const unsigned char character : input) {
        if ((std::isalnum(character) || character == ' ' || character == '-' || character == '_') && result.size() < 16)
            result += static_cast<char>(character);
    }
    const auto first = result.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    const auto last = result.find_last_not_of(' ');
    return result.substr(first, last - first + 1);
}

inline std::string extract_room_code(const std::string& input) {
    std::string candidate;
    for (std::size_t index = 0; index <= input.size(); ++index) {
        const unsigned char character = index < input.size() ? static_cast<unsigned char>(input[index]) : 0;
        if (index < input.size() && room_code_character(character)) {
            candidate += static_cast<char>(std::toupper(character));
        } else {
            if (candidate.size() == 6) return candidate;
            candidate.clear();
        }
    }
    return {};
}

inline std::string json_string_value(const std::string& input, const std::string& key) {
    const auto found = input.find("\"" + key + "\":\"");
    if (found == std::string::npos) return {};
    std::string result;
    std::size_t position = found + key.size() + 4;
    while (position < input.size()) {
        const char character = input[position++];
        if (character == '"') return result;
        if (character != '\\') {
            result += character;
            continue;
        }
        if (position >= input.size()) return {};
        const char escaped = input[position++];
        if (escaped == '"' || escaped == '\\' || escaped == '/')
            result += escaped;
        else if (escaped == 'b')
            result += '\b';
        else if (escaped == 'f')
            result += '\f';
        else if (escaped == 'n')
            result += '\n';
        else if (escaped == 'r')
            result += '\r';
        else if (escaped == 't')
            result += '\t';
        else if (escaped == 'u') {
            if (position + 4 > input.size()) return {};
            unsigned codepoint = 0;
            for (int index = 0; index < 4; ++index) {
                const char digit = input[position++];
                codepoint <<= 4;
                if (digit >= '0' && digit <= '9')
                    codepoint += static_cast<unsigned>(digit - '0');
                else if (digit >= 'a' && digit <= 'f')
                    codepoint += static_cast<unsigned>(digit - 'a' + 10);
                else if (digit >= 'A' && digit <= 'F')
                    codepoint += static_cast<unsigned>(digit - 'A' + 10);
                else
                    return {};
            }
            if (codepoint <= 0x7F)
                result += static_cast<char>(codepoint);
            else if (codepoint <= 0x7FF) {
                result += static_cast<char>(0xC0 | (codepoint >> 6));
                result += static_cast<char>(0x80 | (codepoint & 0x3F));
            } else {
                result += static_cast<char>(0xE0 | (codepoint >> 12));
                result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (codepoint & 0x3F));
            }
        } else
            return {};
    }
    return {};
}

inline bool valid_replay_move(int row, int column, int player) {
    return row >= 0 && row < 6 && column >= 0 && column < 7 && (player == 1 || player == 2);
}

struct ReplayModel {
    std::array<int, 42> board{};
    std::array<std::array<int, 2>, 4> winning_cells{{{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}}};
    int winner{};
    bool draw{};
    bool valid{};
};

template <typename Moves>
ReplayModel build_replay_model(const Moves& moves) {
    ReplayModel model;
    if (moves.empty() || moves.size() > model.board.size()) return model;

    int expected_player = moves.front().player;
    for (const auto& move : moves) {
        if (!valid_replay_move(move.row, move.column, move.player) || move.player != expected_player ||
            model.winner != 0 || model.draw)
            return {};

        int landing_row = 5;
        while (landing_row >= 0 && model.board[landing_row * 7 + move.column] != 0) --landing_row;
        if (landing_row != move.row) return {};
        model.board[move.row * 7 + move.column] = move.player;

        constexpr int directions[][2]{{0, 1}, {1, 0}, {1, 1}, {1, -1}};
        for (const auto& direction : directions) {
            for (int offset = -3; offset <= 0; ++offset) {
                std::array<std::array<int, 2>, 4> cells{};
                bool line = true;
                for (int index = 0; index < 4; ++index) {
                    const int row = move.row + (offset + index) * direction[0];
                    const int column = move.column + (offset + index) * direction[1];
                    cells[index] = {row, column};
                    if (row < 0 || row >= 6 || column < 0 || column >= 7 ||
                        model.board[row * 7 + column] != move.player)
                        line = false;
                }
                if (line) {
                    model.winner = move.player;
                    model.winning_cells = cells;
                    break;
                }
            }
            if (model.winner != 0) break;
        }
        expected_player = expected_player == 1 ? 2 : 1;
    }
    model.draw = model.winner == 0 && moves.size() == model.board.size();
    model.valid = true;
    return model;
}

}  // namespace connect4::online_helpers
