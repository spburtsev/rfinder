#include <cstdio>

#include "networking.hpp"

static const int DEFAULT_SERVER_PORT = 8080;
const char* DEFAULT_SERVER_ADDRESS = "127.0.0.1"; //localhost //8.8.8.8

int main(int argc, char** argv) {
    int port = DEFAULT_SERVER_PORT;
    if (argc > 1) {
        char* cmdline_port = argv[1];
        port = std::atoi(cmdline_port);
    }
    try {
        net::tcp_server server;
        server.address = DEFAULT_SERVER_ADDRESS;
        server.port = port;
        server.listen();
    } catch (const std::exception& e) {
         fprintf(stderr, "Fatal error: %s\n", e.what());
    } catch (...) {
        fprintf(stderr, "Fatal error of unknown type\n");
    }
    return 0;
}
