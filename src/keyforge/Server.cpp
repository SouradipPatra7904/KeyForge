#include "keyforge/Server.hpp"

#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <chrono>
#include <algorithm>

namespace keyforge {

Server::Server(int port) : port_(port) {
    // Example tokens, you can add more
    auth_tokens_ = {"KeyForgeSecret", "AnotherSecretToken"};
}

Server::~Server() {
    if (server_fd_ != -1) {
        close(server_fd_);
    }

    {
        std::lock_guard<std::mutex> lock(workers_mutex_);
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
    }
}

void Server::requestShutdown() {
    shutdown_requested_.store(true);
    if (server_fd_ != -1) {
        shutdown(server_fd_, SHUT_RDWR);
        close(server_fd_);
        server_fd_ = -1;
    }
}

void Server::send_all(int fd, const std::string& msg) {
    size_t total_sent = 0;
    while (total_sent < msg.size()) {
        ssize_t sent = send(fd, msg.data() + total_sent, msg.size() - total_sent, 0);
        if (sent <= 0) break;
        total_sent += sent;
    }
}

void Server::handleClient(int client_fd) {
    connected_clients_++;

    std::chrono::steady_clock::time_point last_active = std::chrono::steady_clock::now();
    char buffer[1024];

    while (true) {
        // Check inactivity timeout (2 minutes)
        auto now = std::chrono::steady_clock::now();
        auto inactive_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - last_active).count();
        if (inactive_seconds > 120) { // 2 minutes
            send_all(client_fd, "INFO: Session expired due to inactivity\n");
            break;
        }

        memset(buffer, 0, sizeof(buffer));
        ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
        if (n < 0) {
            // No data yet, just wait a bit
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (n == 0) break; // client disconnected

        last_active = std::chrono::steady_clock::now();
        std::istringstream iss(buffer);
        std::string cmd, key, value;
        iss >> cmd;

        std::string response;

        // Sensitive command check
        auto requires_auth = [&](const std::string& c) {
            return c == "UPDATE" || c == "DELETE" || c == "SHUTDOWN";
        };

        bool authenticated = false;
        {
            std::lock_guard<std::mutex> lock(auth_mutex_);
            authenticated = client_authenticated_[client_fd];
        }

        if (requires_auth(cmd) && !authenticated) {
            response = "ERROR Unauthorized. Please AUTH first.\n";
            send_all(client_fd, response);
            continue;
        }

        if (cmd == "PUT") {
            iss >> key >> value;
            store_.put(key, value);
            response = "OK\n";
        }
        else if (cmd == "GET") {
            iss >> key;
            auto val = store_.get(key);
            response = val ? *val + "\n" : "NOT_FOUND\n";
        }
        else if (cmd == "GET_KEY") {
            iss >> value;
            auto key_opt = store_.getKeyByValue(value);
            response = key_opt ? ("OK. Key found :" + *key_opt + "\n") : "NOT_FOUND\n";
        }
        else if (cmd == "DELETE") {
            iss >> key;
            bool removed = store_.remove(key);
            response = removed ? "DELETED\n" : "NOT_FOUND\n";
        }
        else if (cmd == "UPDATE") {
            iss >> key >> value;
            bool updated = store_.update(key, value);
            response = updated ? "UPDATED\n" : "NOT_FOUND\n";
        }
        else if (cmd == "SHUTDOWN") {
            response = "Server shutting down...\nType anything and enter to exit this NetCat session.\n";
            send_all(client_fd, response);
            close(client_fd);
            requestShutdown();
            break;
        }
        else if (cmd == "SAVE") {
            std::string filename;
            iss >> filename;
            if (filename.empty()) filename = "keyforge_store.db";
            bool ok = store_.saveToFile(filename);
            response = ok ? "OK Saved\n" : "ERROR Failed to save\n";
        }
        else if (cmd == "LOAD") {
            std::string filename;
            iss >> filename;
            if (filename.empty()) filename = "keyforge_store.db";
            bool ok = store_.loadFromFile(filename);
            response = ok ? "OK Loaded\n" : "ERROR Failed to load\n";
        }
        else if (cmd == "STATS") {
            size_t keys = store_.size();
            response = "Keys: " + std::to_string(keys) + "\n";
            response += "GET hits: " + std::to_string(store_.get_count) + "\n";
            response += "GET misses: " + std::to_string(store_.get_miss_count) + "\n";
            response += "PUTs: " + std::to_string(store_.put_count) + "\n";
            response += "UPDATEs: " + std::to_string(store_.update_count) + "\n";
            response += "DELETEs: " + std::to_string(store_.delete_count) + "\n";
            response += "Connected clients: " + std::to_string(connected_clients_) + "\n";
        }
        else if (cmd == "AUTH") {
            std::string token;
            iss >> token;
            bool valid = false;
            {
                std::lock_guard<std::mutex> lock(auth_mutex_);
                if (auth_tokens_.count(token)) {
                    client_authenticated_[client_fd] = true;
                    valid = true;
                } else {
                    client_authenticated_[client_fd] = false;
                }
            }
            response = valid ? "OK Authenticated\n" : "ERROR Invalid token\n";
        }
        else {
            response = "ERROR: Unknown command\nValid Commands : [GET, PUT, UPDATE, DELETE, SHUTDOWN, AUTH, SAVE, LOAD, STATS, GET_KEY]\n";
        }

        send_all(client_fd, response);
    }

    connected_clients_--;
    std::lock_guard<std::mutex> lock(auth_mutex_);
    client_authenticated_.erase(client_fd);
    close(client_fd);
}

void Server::run() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        perror("socket");
        return;
    }

    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return;
    }

    if (listen(server_fd_, 10) < 0) {
        perror("listen");
        return;
    }

    std::cout << "KeyForge server listening on port " << port_ << "...\n";

    while (!shutdown_requested_.load()) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &len);

        if (client_fd < 0) {
            if (shutdown_requested_.load()) break;
            perror("accept");
            continue;
        }

        std::lock_guard<std::mutex> lock(workers_mutex_);
        workers_.emplace_back(&Server::handleClient, this, client_fd);
    }

    {
        std::lock_guard<std::mutex> lock(workers_mutex_);
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
        workers_.clear();
    }

    std::cout << "Server stopped.\n";
}

} // namespace keyforge
