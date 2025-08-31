#include "../includes_this/keyforge/Server.hpp"
#include <iostream>
#include <csignal>

using namespace keyforge;

static Server* g_server = nullptr;

// Signal handler for Ctrl+C
void handle_sigint(int) {
    if (g_server) {
        std::cout << "\n[Main] Caught SIGINT, shutting down server..." << std::endl;
        g_server->requestShutdown();  // <-- call the public method, not shutdown_requested_
    }
}

int main() {
    int port = 4545;

    try {
        Server server(port);
        g_server = &server;

        // Register Ctrl+C handler
        std::signal(SIGINT, handle_sigint);

        std::cout << "[Main] Starting KeyForge server on port " << port << "...\n";
        server.run();
        std::cout << "[Main] Server stopped cleanly.\n";
    }
    catch (const std::exception& e) {
        std::cerr << "[Main] Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
