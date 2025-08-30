#include "keyforge/Server.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace keyforge {

// static atomic initialization
std::atomic<bool> Server::shutdown_requested_{false};

Server::Server(int port) : port_(port) {}

void Server::run() {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                   &opt, sizeof(opt))) {
        perror("setsockopt failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    // Bind
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Listen
    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    std::cout << "KeyForge server listening on port " << port_ << "...\n";

    while (!shutdown_requested_) {
        int client_fd = accept(server_fd, (struct sockaddr*)&address, &addrlen);
        if (client_fd < 0) {
            if (shutdown_requested_) break; // stop gracefully
            perror("accept failed");
            continue;
        }

        // Spawn a thread per client
        client_threads_.emplace_back(&Server::handle_client, this, client_fd);
    }

    // Join all client threads before exiting
    for (auto& t : client_threads_) {
        if (t.joinable()) t.join();
    }

    close(server_fd);
    std::cout << "Server shut down gracefully.\n";
}

void Server::handle_client(int client_fd) {
    char buffer[1024] = {0};

    while (!shutdown_requested_) {
        ssize_t valread = read(client_fd, buffer, sizeof(buffer) - 1);
        if (valread <= 0) break; // client disconnected
        buffer[valread] = '\0';

        std::string cmd(buffer);
        std::string response;

        // trim newline characters
        if (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r'))
            cmd.pop_back();

        // Command parsing
        if (cmd.rfind("PUT", 0) == 0) {
            size_t key_start = cmd.find(" ") + 1;
            size_t value_start = cmd.find(" ", key_start) + 1;
            if (key_start != std::string::npos && value_start != std::string::npos) {
                std::string key = cmd.substr(key_start, value_start - key_start - 1);
                std::string value = cmd.substr(value_start);
                store_.put(key, value);
                response = "OK\n";
            } else {
                response = "ERROR: Invalid PUT format\n";
            }
        } else if (cmd.rfind("GET", 0) == 0) {
            std::string key = cmd.substr(4);
            auto val = store_.get(key);
            response = val.has_value() ? val.value() + "\n" : "NOT_FOUND\n";
        }
        else if (cmd.rfind("DELETE", 0) == 0) {
            std::string key = cmd.substr(7);
            if (store_.remove(key))
                response = "DELETED\n";
            else
                response = "NOT_FOUND\n";
        }
        else if (cmd.rfind("UPDATE", 0) == 0) {
            size_t key_start = cmd.find(" ") + 1;
            size_t value_start = cmd.find(" ", key_start) + 1;
            if (key_start != std::string::npos && value_start != std::string::npos) {
                std::string key = cmd.substr(key_start, value_start - key_start - 1);
                std::string value = cmd.substr(value_start);
                if (store_.remove(key)) {
                    store_.put(key, value);
                    response = "UPDATED\n";
                } else {
                    response = "NOT_FOUND\n";
                }
            } else {
                response = "ERROR: Invalid UPDATE format\n";
            }
        } else if (cmd.rfind("SHUTDOWN", 0) == 0) {
            response = "Server shutting down...\n";
            shutdown_requested_ = true;
            send(client_fd, response.c_str(), response.size(), 0);
            break;
        } else {
            response = "ERROR: Unknown command\n Valid Commands : [GET, PUT, UPDATE, DELETE, SHUTDOWN]";
        }

        // send response back to client
        if (!response.empty()) {
            send(client_fd, response.c_str(), response.size(), 0);
        }
    }

    close(client_fd);
}

} // namespace keyforge
