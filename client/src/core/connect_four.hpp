#pragma once

#include <array>
#include <optional>
#include <vector>

namespace connect4 {

constexpr int columns = 7;
constexpr int rows = 6;

enum class Player { none, coral, gold };

struct Move {
    int row{};
    int column{};
    Player player{Player::none};
};

class Game {
   public:
    using Grid = std::array<std::array<Player, columns>, rows>;

    std::optional<Move> drop(int column) {
        if (column < 0 || column >= columns || winner_ != Player::none || draw_) return std::nullopt;
        for (int row = rows - 1; row >= 0; --row) {
            if (grid_[row][column] != Player::none) continue;
            const Move move{row, column, current_};
            grid_[row][column] = current_;
            history_.push_back(move);
            if (!find_win(move)) {
                draw_ = history_.size() == rows * columns;
                if (!draw_) current_ = other(current_);
            }
            return move;
        }
        return std::nullopt;
    }

    bool undo() {
        if (history_.empty()) return false;
        const Move move = history_.back();
        history_.pop_back();
        grid_[move.row][move.column] = Player::none;
        current_ = move.player;
        winner_ = Player::none;
        draw_ = false;
        winning_cells_.fill({-1, -1});
        return true;
    }

    void reset() {
        grid_ = {};
        history_.clear();
        current_ = Player::coral;
        winner_ = Player::none;
        draw_ = false;
        winning_cells_.fill({-1, -1});
    }

    const Grid& grid() const { return grid_; }

    Player current() const { return current_; }

    Player winner() const { return winner_; }

    bool draw() const { return draw_; }

    bool over() const { return winner_ != Player::none || draw_; }

    const std::array<std::array<int, 2>, 4>& winning_cells() const { return winning_cells_; }

    std::size_t move_count() const { return history_.size(); }

    // Applies the server-authoritative board in online games. Local games still
    // use drop()/undo(), so their history remains untouched.
    void sync_online(
        const std::array<int, rows * columns>& board, int turn, int winner, bool draw,
        const std::array<std::array<int, 2>, 4>& winning_cells
    ) {
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < columns; ++column)
                grid_[row][column] = board[row * columns + column] == 1   ? Player::coral
                                     : board[row * columns + column] == 2 ? Player::gold
                                                                          : Player::none;
        current_ = turn == 2 ? Player::gold : Player::coral;
        winner_ = winner == 1 ? Player::coral : winner == 2 ? Player::gold : Player::none;
        draw_ = draw;
        winning_cells_ = winning_cells;
        history_.clear();
    }

   private:
    Grid grid_{};
    std::vector<Move> history_;
    Player current_{Player::coral};
    Player winner_{Player::none};
    bool draw_{};
    std::array<std::array<int, 2>, 4> winning_cells_{{{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}}};

    static Player other(Player player) { return player == Player::coral ? Player::gold : Player::coral; }

    bool find_win(Move move) {
        constexpr int directions[][2]{{0, 1}, {1, 0}, {1, 1}, {1, -1}};
        for (const auto& direction : directions) {
            std::vector<std::array<int, 2>> line{{move.row, move.column}};
            for (int sign : {-1, 1}) {
                int row = move.row + direction[0] * sign;
                int col = move.column + direction[1] * sign;
                while (row >= 0 && row < rows && col >= 0 && col < columns && grid_[row][col] == move.player) {
                    if (sign < 0)
                        line.insert(line.begin(), {row, col});
                    else
                        line.push_back({row, col});
                    row += direction[0] * sign;
                    col += direction[1] * sign;
                }
            }
            if (line.size() >= 4) {
                winner_ = move.player;
                for (int i = 0; i < 4; ++i) winning_cells_[i] = line[i];
                return true;
            }
        }
        return false;
    }
};

}  // namespace connect4
