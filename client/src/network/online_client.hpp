#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// raylib intentionally uses names such as Rectangle and CloseWindow.  WinHTTP
// needs the Windows base types, but none of the GDI/User drawing API.
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "online_helpers.hpp"

namespace online {

constexpr wchar_t host[] = L"connect-four-relay.ayushdedhia25.workers.dev";
constexpr std::size_t max_server_message_bytes = 64 * 1024;

struct ReplayMove {
    int row{};
    int column{};
    int player{};
};

struct State {
    std::array<int, 42> board{};
    int turn{1};
    int winner{};
    bool draw{};
    int coral_score{};
    int gold_score{};
    int draw_score{};
    int rematch_votes{};
    std::vector<ReplayMove> history;
    std::vector<ReplayMove> last_replay;
    std::array<std::string, 2> names{"Player one", "Player two"};
    std::array<std::array<int, 2>, 4> winning_cells{{{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}}};
};

struct Event {
    enum class Type { welcome, move, state, presence, cursor, reaction, latency, error, disconnected } type;
    State state{};
    int row{-1};
    int column{-1};
    int player{};
    int seat{};
    bool rival_connected{};
    int latency_ms{-1};
    std::string reaction;
    std::string token;
    std::string error;
};

class Client {
   public:
    ~Client() { disconnect(); }

    Client(const Client&) = delete;
    Client() = default;

    std::optional<std::string> create_room() {
        const auto response = request(L"POST", L"/rooms");
        const auto room = connect4::online_helpers::json_string_value(response, "room");
        return room.empty() ? std::nullopt : std::optional<std::string>{room};
    }

    bool connect(const std::string& room, const std::string& name, const std::string& token = "") {
        disconnect();
        room_ = uppercase(room);

        const std::string path =
            "/rooms/" + room_ + "?name=" + url_encode(name) + (token.empty() ? "" : "&token=" + url_encode(token));

        session_ = WinHttpOpen(
            L"ConnectFourNeon/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0
        );

        if (!session_) return false;

        WinHttpSetTimeouts(session_, 5000, 5000, 5000, 5000);

        connection_ = WinHttpConnect(session_, host, INTERNET_DEFAULT_HTTPS_PORT, 0);

        if (!connection_) return fail_connect();

        request_ = WinHttpOpenRequest(
            connection_, L"GET", widen(path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE
        );

        if (!request_) return fail_connect();
        if (!WinHttpSetOption(request_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) return fail_connect();
        if (!WinHttpSendRequest(request_, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
            return fail_connect();

        if (!WinHttpReceiveResponse(request_, nullptr)) return fail_connect();

        // The 5s session receive timeout guards the HTTP handshake above, but
        // the WebSocket inherits it for WinHttpWebSocketReceive and it would
        // sever any link that stays quiet longer than one 5s ping interval.
        // Raise it (on the request handle, pre-upgrade, so inheritance is
        // guaranteed) far above the ping cadence yet keep it finite: a dead
        // network must still wake the receiver thread so disconnect() can
        // join it. keep_alive() flags a dead link after 15s of ping silence;
        // 20s bounds how long the receiver can linger past that.
        WinHttpSetTimeouts(request_, 5000, 5000, 5000, 20000);

        HINTERNET upgraded = WinHttpWebSocketCompleteUpgrade(request_, 0);
        WinHttpCloseHandle(request_);
        request_ = nullptr;

        if (!upgraded) return fail_connect();

        {
            std::lock_guard lock(socket_mutex_);
            socket_ = upgraded;
        }
        connected_ = true;
        ping_sent_ms_ = 0;
        next_ping_ms_ = 0;
        ping_timeout_reported_ = false;
        receiver_ = std::thread([this] { receive_loop(); });
        return true;
    }

    void disconnect() {
        connected_ = false;
        {
            // Shutdown uses only the WebSocket send side and is allowed while
            // the receiver thread is blocked in WinHttpWebSocketReceive. Close
            // is not.
            std::lock_guard lock(socket_mutex_);
            if (socket_) WinHttpWebSocketShutdown(socket_, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
        }
        if (receiver_.joinable()) receiver_.join();
        close_handles();
    }

    bool connected() const { return connected_; }

    const std::string& room() const { return room_; }

    void send_move(int column) { send("{\"type\":\"move\",\"column\":" + std::to_string(column) + "}"); }

    void send_cursor(int column) { send("{\"type\":\"cursor\",\"column\":" + std::to_string(column) + "}"); }

    void send_reaction(const std::string& reaction) {
        send("{\"type\":\"reaction\",\"reaction\":\"" + reaction + "\"}");
    }

    void send_rematch() { send("{\"type\":\"rematch\"}"); }

    void keep_alive() {
        const auto now =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        const long long sent = ping_sent_ms_.load();
        if (connected_ && sent != 0 && now - sent >= 10000 && !ping_timeout_reported_.exchange(true)) {
            Event event{};
            event.type = Event::Type::latency;
            event.latency_ms = -1;
            push(std::move(event));
        }
        if (connected_ && sent != 0 && now - sent >= 15000) {
            // The relay stopped answering pings entirely: declare the link
            // dead so the application reconnects instead of idling on a
            // silent socket until the 20s receive timeout fires.
            signal_disconnect();
            return;
        }
        if (connected_ && sent == 0 && now >= next_ping_ms_.load()) {
            ping_sent_ms_ = now;
            send("ping");
        }
    }

    std::vector<Event> poll() {
        std::lock_guard lock(events_mutex_);
        auto copy = std::move(events_);
        events_.clear();
        return copy;
    }

   private:
    HINTERNET session_{};
    HINTERNET connection_{};
    HINTERNET request_{};
    HINTERNET socket_{};
    std::thread receiver_;
    std::atomic_bool connected_{false};
    // Guards socket_ against concurrent teardown/recreation by the async
    // connect task while the main thread sends. The receiver thread reads the
    // handle without the lock: it only exists between the locked assignment in
    // connect() and the join in disconnect(), during which socket_ is stable.
    std::mutex socket_mutex_;
    std::mutex events_mutex_;
    std::vector<Event> events_;
    std::string room_;
    std::atomic<long long> ping_sent_ms_{0};
    std::atomic<long long> next_ping_ms_{0};
    std::atomic_bool ping_timeout_reported_{false};

    bool fail_connect() {
        close_handles();
        return false;
    }

    void close_handles() {
        {
            std::lock_guard lock(socket_mutex_);
            if (socket_) {
                WinHttpCloseHandle(socket_);
                socket_ = nullptr;
            }
        }

        if (request_) {
            WinHttpCloseHandle(request_);
            request_ = nullptr;
        }

        if (connection_) {
            WinHttpCloseHandle(connection_);
            connection_ = nullptr;
        }

        if (session_) {
            WinHttpCloseHandle(session_);
            session_ = nullptr;
        }
    }

    void send(const std::string& message) {
        DWORD result;
        {
            // The reconnect task tears down and recreates socket_ on a worker
            // thread; the lock keeps this send from touching a dying handle.
            std::lock_guard lock(socket_mutex_);
            if (!socket_ || !connected_) return;
            result = WinHttpWebSocketSend(
                socket_, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, const_cast<char*>(message.data()),
                static_cast<DWORD>(message.size())
            );
        }
        if (result != NO_ERROR) signal_disconnect();
    }

    void receive_loop() {
        std::string message;
        while (connected_ && socket_) {
            char buffer[8192];
            DWORD received{};
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
            const DWORD result = WinHttpWebSocketReceive(socket_, buffer, sizeof(buffer), &received, &type);
            if (result != NO_ERROR || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
            if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) {
                if (message.size() + received > max_server_message_bytes) break;
                message.append(buffer, received);
            } else if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                if (message.size() + received > max_server_message_bytes) break;
                message.append(buffer, received);
                parse_event(message);
                message.clear();
            }
        }
        signal_disconnect();
    }

    void signal_disconnect() {
        if (!connected_.exchange(false)) return;
        Event event{};
        event.type = Event::Type::disconnected;
        push(std::move(event));
    }

    void parse_event(const std::string& json) {
        if (json == "pong") {
            const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch()
            )
                                 .count();
            Event event{};
            event.type = Event::Type::latency;
            const long long sent = ping_sent_ms_.exchange(0);
            if (sent == 0) return;
            event.latency_ms = static_cast<int>(std::max(0LL, now - sent));
            next_ping_ms_ = now + 5000;
            ping_timeout_reported_ = false;
            push(std::move(event));
            return;
        }

        Event event{};
        const auto type = connect4::online_helpers::json_string_value(json, "type");
        if (type == "welcome")
            event.type = Event::Type::welcome;
        else if (type == "move")
            event.type = Event::Type::move;
        else if (type == "state")
            event.type = Event::Type::state;
        else if (type == "presence")
            event.type = Event::Type::presence;
        else if (type == "cursor")
            event.type = Event::Type::cursor;
        else if (type == "reaction")
            event.type = Event::Type::reaction;
        else {
            event.type = Event::Type::error;
            event.error = connect4::online_helpers::json_string_value(json, "error");
            push(std::move(event));
            return;
        }
        event.seat = json_int(json, "seat", 0);
        event.token = connect4::online_helpers::json_string_value(json, "token");
        // A move payload nests its coordinates in "move":{...}; parse from that
        // object only, so identical keys inside the game state (history moves)
        // can never shadow them regardless of serialization order.
        std::string scope = json;
        if (event.type == Event::Type::move) {
            const auto move_object = json.find("\"move\":{");
            const auto close = move_object == std::string::npos ? std::string::npos : json.find('}', move_object);
            if (close != std::string::npos) scope = json.substr(move_object, close - move_object + 1);
        }
        event.row = json_int(scope, "row", -1);
        event.column = json_int(scope, "column", -1);
        event.player = json_int(scope, "player", 0);
        event.rival_connected = json.find("\"connected\":[1,2]") != std::string::npos;
        event.reaction = connect4::online_helpers::json_string_value(json, "reaction");
        event.state = parse_state(json);
        push(std::move(event));
    }

    void push(Event event) {
        std::lock_guard lock(events_mutex_);
        events_.push_back(std::move(event));
    }

    // Returns the index of the ']' closing the '[' at `open`, tolerating
    // nested arrays. Only used on arrays of numbers, so brackets inside
    // strings cannot occur.
    static std::size_t matching_bracket(const std::string& input, std::size_t open) {
        int depth = 0;
        for (std::size_t index = open; index < input.size(); ++index) {
            if (input[index] == '[')
                ++depth;
            else if (input[index] == ']' && --depth == 0)
                return index;
        }
        return std::string::npos;
    }

    static State parse_state(const std::string& json) {
        State state;
        const auto board = json.find("\"board\":[");
        if (board != std::string::npos) {
            const auto end = matching_bracket(json, board + 8);
            std::size_t pos = board + 9;
            for (auto& cell : state.board) cell = next_int(json, pos, 0, end);
        }
        state.turn = json_int(json, "turn", 1);
        state.winner = json_int(json, "winner", 0);
        state.draw = json_bool(json, "draw", false);
        state.coral_score = json_int(json, "coralScore", 0);
        state.gold_score = json_int(json, "goldScore", 0);
        state.draw_score = json_int(json, "drawScore", 0);
        state.rematch_votes = json_int(json, "rematchVotes", 0);
        state.history = parse_moves(json, "history");
        state.last_replay = parse_moves(json, "lastReplay");
        const auto first_name = connect4::online_helpers::json_string_value(json, "1");
        const auto second_name = connect4::online_helpers::json_string_value(json, "2");
        if (!first_name.empty()) state.names[0] = first_name;
        if (!second_name.empty()) state.names[1] = second_name;
        const auto cells = json.find("\"winningCells\":[");
        if (cells != std::string::npos) {
            // Bounded by the outer bracket: an empty array must leave the
            // {-1,-1} defaults instead of consuming digits from later keys.
            const auto end = matching_bracket(json, cells + 15);
            std::size_t pos = cells + 16;
            for (auto& cell : state.winning_cells) {
                cell[0] = next_int(json, pos, -1, end);
                cell[1] = next_int(json, pos, -1, end);
            }
        }
        return state;
    }

    static std::vector<ReplayMove> parse_moves(const std::string& json, const std::string& key) {
        std::vector<ReplayMove> moves;
        const auto start = json.find("\"" + key + "\":[");
        if (start == std::string::npos) return moves;
        const auto end = json.find(']', start);
        if (end == std::string::npos) return moves;
        std::size_t position = start;
        while ((position = json.find('{', position)) != std::string::npos && position < end) {
            const auto close = json.find('}', position);
            if (close == std::string::npos || close > end) break;
            const std::string item = json.substr(position, close - position + 1);
            const ReplayMove move{json_int(item, "row", -1), json_int(item, "column", -1), json_int(item, "player", 0)};
            if (connect4::online_helpers::valid_replay_move(move.row, move.column, move.player)) moves.push_back(move);
            position = close + 1;
        }
        return moves;
    }

    static int next_int(const std::string& input, std::size_t& pos, int fallback, std::size_t end = std::string::npos) {
        const std::size_t limit = std::min(end, input.size());
        while (pos < limit && input[pos] != '-' && !std::isdigit(static_cast<unsigned char>(input[pos]))) ++pos;
        if (pos >= limit) return fallback;
        bool negative = input[pos] == '-';
        if (negative) ++pos;
        int value = 0;
        bool any = false;
        while (pos < limit && std::isdigit(static_cast<unsigned char>(input[pos]))) {
            const int digit = input[pos++] - '0';
            if (value > (std::numeric_limits<int>::max() - digit) / 10) {
                while (pos < limit && std::isdigit(static_cast<unsigned char>(input[pos]))) ++pos;
                return fallback;
            }
            any = true;
            value = value * 10 + digit;
        }
        return any ? (negative ? -value : value) : fallback;
    }

    static int json_int(const std::string& input, const std::string& key, int fallback) {
        const auto found = input.find("\"" + key + "\":");
        if (found == std::string::npos) return fallback;
        std::size_t pos = found + key.size() + 3;
        return next_int(input, pos, fallback);
    }

    static bool json_bool(const std::string& input, const std::string& key, bool fallback) {
        const auto found = input.find("\"" + key + "\":");
        if (found == std::string::npos) return fallback;
        const auto pos = found + key.size() + 3;
        return input.compare(pos, 4, "true") == 0 ? true : input.compare(pos, 5, "false") == 0 ? false : fallback;
    }

    static std::string uppercase(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        return value;
    }

    static std::string url_encode(const std::string& value) {
        std::string result;
        constexpr char hex[] = "0123456789ABCDEF";
        for (unsigned char c : value) {
            if (std::isalnum(c) || c == '-' || c == '_')
                result += static_cast<char>(c);
            else {
                result += '%';
                result += hex[c >> 4];
                result += hex[c & 15];
            }
        }
        return result;
    }

    static std::wstring widen(const std::string& value) {
        if (value.empty()) return {};
        const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (size <= 0) return {};
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
        return result;
    }

    std::string request(const wchar_t* method, const wchar_t* path) {
        HINTERNET session = WinHttpOpen(
            L"ConnectFourNeon/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0
        );
        if (!session) return {};
        WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
        HINTERNET connection = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        HINTERNET request = connection ? WinHttpOpenRequest(
                                             connection, method, path, nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE
                                         )
                                       : nullptr;
        std::string response;
        if (request &&
            WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            while (true) {
                DWORD available{};
                if (!WinHttpQueryDataAvailable(request, &available) || !available) break;
                if (available > max_server_message_bytes - response.size()) {
                    response.clear();
                    break;
                }
                const auto offset = response.size();
                response.resize(offset + available);
                DWORD read{};
                if (!WinHttpReadData(request, response.data() + offset, available, &read)) {
                    response.clear();
                    break;
                }
                response.resize(offset + read);
            }
        }
        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }
};

}  // namespace online
