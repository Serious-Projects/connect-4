#include <chrono>
#include <iostream>
#include <thread>

#include "network/online_client.hpp"

int main() {
    online::Client client;
    client.begin_operation();
    const auto room = client.create_room();
    if (!room) {
        std::cerr << "Could not create a live relay room.\n";
        return 1;
    }
    if (!client.connect(*room, "Native smoke")) {
        std::cerr << "Could not connect the native WinHTTP client.\n";
        return 1;
    }

    // Let the receiver enter its blocking receive and queue the welcome event.
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const auto started = std::chrono::steady_clock::now();
    client.disconnect();
    const auto elapsed = std::chrono::steady_clock::now() - started;

    if (client.connected()) {
        std::cerr << "Client still reports connected after disconnect.\n";
        return 1;
    }
    if (!client.poll().empty()) {
        std::cerr << "Disconnect left stale relay events queued.\n";
        return 1;
    }
    if (elapsed > std::chrono::seconds(3)) {
        std::cerr << "WebSocket disconnect took too long.\n";
        return 1;
    }

    std::cout << "Native online lifecycle smoke passed for room " << *room << ".\n";
    return 0;
}
