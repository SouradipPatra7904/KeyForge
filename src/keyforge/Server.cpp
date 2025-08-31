#include "keyforge/Server.hpp"

#include <unordered_map>
#include <iostream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>

namespace keyforge {

Server::Server(int port) : port_(port) {}

Server::~Server() {
    if (server_fd_ != -1) {
        close(server_fd_);
    }

    {
        std::lock_guard<std::mutex> lock(workers_mutex_);
        for (auto& w : workers_) {
            if (w.joinable()) {
                w.join();
            }
        }
    }
}

void Server::requestShutdown() {
    shutdown_requested_.store(true);
    // Closing listening socket will break accept()
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
    char buffer[1024];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            close(client_fd);
            return;
        }

        std::istringstream iss(buffer);
        std::string cmd, key, value;
        iss >> cmd;

        std::string response;

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

        /**
         * 
         * else if (cmd == "GET_KEY") {
            std::string value;
            iss >> value;
            bool found = false;
            for (const auto& pair : store_) {
                if (pair.second == value) {
                    response = "OK " + pair.first + "\n";
                    found = true;
                    break;
                }
            }
            if (!found) {
                response = "NOT_FOUND\n";
            }
        }
         */
        
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
            return;
        }
        else {
            response = "ERROR: Unknown command\nValid Commands : [GET, PUT, UPDATE, DELETE, SHUTDOWN]\n";
        }

        send_all(client_fd, response);
    }
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
            if (shutdown_requested_.load()) break; // caused by shutdown()
            perror("accept");
            continue;
        }

        std::lock_guard<std::mutex> lock(workers_mutex_);
        workers_.emplace_back(&Server::handleClient, this, client_fd);
    }

    {
        std::lock_guard<std::mutex> lock(workers_mutex_);
        for (auto& w : workers_) {
            if (w.joinable()) {
                w.join();
            }
        }
        workers_.clear();
    }

    std::cout << "Server stopped.\n";
}

} // namespace keyforge
