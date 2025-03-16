#include <string>
#include <cstring>
#include "networking.hpp"
#include "threading.hpp"

using namespace std::string_literals;

#ifdef __unix__

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

static void unix_send_response(const proto::file_seach_response& response) {
    fprintf(stdout, "Response:\n  Status: %s\n  Payload: %s\n\n", 
        proto::to_string(response.status).c_str(),
        response.payload.c_str());
}

static proto::file_search_request unix_receive_request(int server_socket) {
    int connection = accept(server_socket, 0, 0);
    if (connection == -1) {
        throw std::runtime_error("Could not accept connection");
    }
    char buffer[1024] = {0};
    int valread = read(connection, buffer, sizeof(buffer));
    if (valread == -1) {
        throw std::runtime_error("Could not read from connection");
    }
    return proto::file_search_request::parse_from_buffer(buffer, valread);
}


struct socket_guard final {
    int fd;

    socket_guard() {
        this->fd = socket(AF_INET, SOCK_STREAM, 0);
        if (this->fd == -1) {
            throw std::runtime_error("Could not create socket");
        }
    }

    ~socket_guard() {
        if (this->fd) {
            close(this->fd);
        }
    }
};

static void unix_listen(const net::tcp_server& server) {
    socket_guard server_socket;

    sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    inet_pton(AF_INET, server.address, &server_address.sin_addr);
    server_address.sin_port = htons(server.port);

    if (bind(server_socket.fd, (sockaddr*)&server_address, sizeof(server_address)) == -1) {
        throw std::runtime_error("Could not bind socket: "s + strerror(errno));
    }
    if ((listen(server_socket.fd, 5)) != 0) { 
        throw std::runtime_error("Listen failed: "s + strerror(errno));
    } 
    while (true) {
        auto req = unix_receive_request(server_socket.fd);
        fprintf(stdout, "Received request: filename: \"%s\", Root path: \"%s\"\n", 
            req.filename.c_str(), req.root_path.c_str());

        threading::find_file_task(req, unix_send_response);
    }
}

#endif

void net::tcp_server::listen() const {
    printf("Listening on %s:%d\n", this->address, this->port);

    #ifdef __unix__
    unix_listen(*this);
    #else
    #error "Unsupported platform"
    #endif
}
