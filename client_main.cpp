#include <stdio.h>
#include <string.h>

#include "protocol.hpp"

struct tcp_server_info final {
    char address[256];
    int port;
};

int main() {
    tcp_server_info server_info = {};
    puts("Enter server address: ");

    bool parsed = false;
    while (!parsed) {
        if (fgets(server_info.address, sizeof(server_info.address), stdin) == NULL) {
            fprintf(stderr, "Could not read entered address\n");
            memset(server_info.address, 0, sizeof(server_info.address));
            continue;
        }
        char* newline = strchr(server_info.address, '\n');
        if (newline != NULL) {
            *newline = '\0';
        }
        parsed = true;
    }
    puts("Enter server port: ");
    parsed = false;
    while (!parsed) {
        if (scanf("%d", &server_info.port) != 1) {
            fprintf(stderr, "Could not read entered port\n");
            continue;
        }
        parsed = true;
    }
    printf("Server address: %s\nServer port: %d\n", server_info.address, server_info.port);

    return 0;
}
