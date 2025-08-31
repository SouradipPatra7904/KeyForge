#ifndef KEYFORGE_SERVER_HPP
#define KEYFORGE_SERVER_HPP

#include "Store.hpp"
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>

namespace keyforge {

class Server {
public:
    explicit Server(int port);
    ~Server();

    // Main entry point
    void run();

    // Externally trigger shutdown (e.g., from signal handler)
    void requestShutdown();

private:
    int port_;
    Store store_;

    std::atomic<bool> shutdown_requested_{false};
    int server_fd_{-1};

    std::vector<std::thread> workers_;
    std::mutex workers_mutex_;

    void handleClient(int client_fd);

    // Utility
    static void send_all(int fd, const std::string& msg);

    // Number of Connected clients counter
    std::atomic<size_t> connected_clients_{0};

    // Authentication variables :
    std::unordered_set<std::string> auth_tokens_;  // set of valid tokens
    std::unordered_map<int, bool> client_authenticated_; // maps client_fd -> bool
    std::mutex auth_mutex_;

};

} // namespace keyforge

#endif // KEYFORGE_SERVER_HPP
