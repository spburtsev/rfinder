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

struct unix_request_handle final {
    proto::file_search_request req;
    int connection_fd;
};

static void unix_send_response(
    const unix_request_handle& handle,
    const proto::file_search_response& response
) {
    auto serialized_res = response.serialize();
    write(handle.connection_fd, serialized_res.data(), serialized_res.size()); 
}

static unix_request_handle unix_receive_request(int server_socket) {
    int connection = accept(server_socket, 0, 0);
    if (connection == -1) {
        throw std::runtime_error("Could not accept connection");
    }
    char buffer[1024] = {0};
    int valread = read(connection, buffer, sizeof(buffer));
    if (valread == -1) {
        throw std::runtime_error("Could not read from connection");
    }

    unix_request_handle handle;
    handle.connection_fd = connection;
    handle.req = proto::file_search_request::parse_from_buffer(buffer, valread);
    return handle;
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
        auto h = unix_receive_request(server_socket.fd);
        fprintf(stdout, "Received request: filename: \"%s\", Root path: \"%s\"\n", 
            h.req.filename.c_str(), h.req.root_path.c_str());

        auto task_callback = [&h](const proto::file_search_response& res) {
            unix_send_response(h, res);
        };

        threading::find_file_task(h.req, task_callback);
        // close(h.fd);
    }
}
#elif defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include "fs.hpp"
#include <iostream>

static void windows_listen(const net::tcp_server& server) {
    proto::file_search_request req;
    req.filename = "client_main.cpp";
    auto result = fs::find_file(req);
    std::cout << result << std::endl;
}
#endif

void net::tcp_server::listen() const {
    printf("Listening on %s:%d\n", this->address, this->port);

    #ifdef __unix__
    unix_listen(*this);
    #else
    windows_listen(*this);
    #endif
}
