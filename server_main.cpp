#include <cstdio>

#include "protocol.hpp"
#include "networking.hpp"
#include "threading.hpp"

static const int DEFAULT_SERVER_PORT = 8080;
const char* DEFAULT_SERVER_ADDRESS = "127.0.0.1"; //localhost //8.8.8.8

int main(int argc, char** argv) {
    int port = DEFAULT_SERVER_PORT;
    if (argc > 1) {
        char* cmdline_port = argv[1];
        port = std::atoi(cmdline_port);
    }
    net::tcp_server server;
    server.address = DEFAULT_SERVER_ADDRESS;
    server.port = port;
    server.listen();
    return 0;
}
