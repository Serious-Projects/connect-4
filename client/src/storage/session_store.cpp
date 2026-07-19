#include "storage/session_store.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include "network/online_helpers.hpp"

namespace app {
namespace {

std::filesystem::path session_path() {
    if (const char* local_app_data = std::getenv("LOCALAPPDATA"))
        return std::filesystem::path(local_app_data) / "NeonConnectFour" / "session.txt";
    return "connect_four_session.txt";
}

}  // namespace

SavedSession load_session() {
    SavedSession session;
    std::ifstream input(session_path());
    std::getline(input, session.room);
    std::getline(input, session.token);
    std::getline(input, session.name);
    session.room = connect4::online_helpers::extract_room_code(session.room);
    if (session.token.size() > 80) session.token.resize(80);
    session.name = connect4::online_helpers::sanitize_player_name(session.name);
    return session;
}

void save_session(const SavedSession& session) {
    const auto path = session_path();
    std::error_code error;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::trunc);
    if (output) output << session.room << '\n' << session.token << '\n' << session.name << '\n';
}

}  // namespace app
