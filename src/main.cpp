#include "keyforge/Server.hpp"

int main() {
    keyforge::Server server(5000);
    server.run();
    return 0;
}
