#pragma once

#include <string>

namespace app {

struct SavedSession {
    std::string room;
    std::string token;
    std::string name;
};

SavedSession load_session();

void save_session(const SavedSession& session);

}  // namespace app
