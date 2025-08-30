#pragma once

#include "Store.hpp"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace keyforge {

/**
 * @brief KeyForge server class
 * 
 * Supports basic commands:
 *   - PUT key value
 *   - GET key
 *   - DEL key
 *   - UPDATE key new_value
 *   - SHUTDOWN
 * 
 * Handles multiple clients concurrently.
 */
class Server {
public:
    explicit Server(int port);

    // start the server loop
    void run();

private:
    int port_;
    Store store_;

    // shutdown flag shared across threads
    static std::atomic<bool> shutdown_requested_;

    // track client threads for cleanup
    std::vector<std::thread> client_threads_;

    // per-client handler
    void handle_client(int client_fd);
};

} // namespace keyforge
