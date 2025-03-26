#include <string>
#include <cstring>
#include <cassert>
#include <stdexcept>
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
#include <iostream>
#include <cstdio>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment (lib, "Ws2_32.lib")

struct win32_server_state final {
    addrinfo* obtained_addrinfo = 0;
    SOCKET    listen_socket = INVALID_SOCKET;

    ~win32_server_state() {
        if (this->obtained_addrinfo) {
            freeaddrinfo(this->obtained_addrinfo);
            this->obtained_addrinfo = 0;
        }
        if (this->listen_socket != INVALID_SOCKET) {
            closesocket(this->listen_socket);
            this->listen_socket = INVALID_SOCKET;
        }
        WSACleanup();
    }

    std::runtime_error err(const char* op) const {
        return std::runtime_error(std::string(op) + " failed with error: " + std::to_string(WSAGetLastError()));
    }

    void proccess_request(SOCKET client_socket) const {
        assert(this->listen_socket != INVALID_SOCKET);
        assert(client_socket != INVALID_SOCKET);
        char recvbuf[1024];
        constexpr int recvbuflen = sizeof(recvbuf);
        int bytes_recv = 0;
        do {
            ZeroMemory(recvbuf, sizeof(recvbuf));
            bytes_recv = recv(client_socket, recvbuf, recvbuflen, 0);
            if (bytes_recv > 0) {
                auto req = proto::file_search_request::parse_from_buffer(recvbuf, bytes_recv);

                auto callback = [client_socket](const proto::file_search_response& res) {
                    assert(client_socket != INVALID_SOCKET);
                    auto serialized_res = res.serialize();
                    auto sent = send(client_socket, serialized_res.data(), serialized_res.size(), 0);
                    if (sent == SOCKET_ERROR) {
                        throw std::runtime_error("send_error");
                    }
                };
                threading::find_file_task(req, callback);
            } else if (bytes_recv == 0) {
                printf("Connection closing...\n");
            } else  {
                throw this->err("recv");
            }
        } while (bytes_recv > 0);
        // shutdown the connection since we're done
        auto sd_result = shutdown(client_socket, SD_SEND);
        if (sd_result == SOCKET_ERROR) {
            throw this->err("shutdown");
        }
    }
};

static void win32_listen(const net::tcp_server& server) {
    WSADATA ws_data;
    auto startup_result = WSAStartup(MAKEWORD(2,2), &ws_data);
    if (startup_result != 0) {
        throw std::runtime_error("Failed to start server socket");
    }
    addrinfo hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    win32_server_state sstate;
    auto getaddr_result = getaddrinfo(NULL, std::to_string(server.port).c_str(), &hints, &sstate.obtained_addrinfo);
    if (getaddr_result != 0 ) {
        throw sstate.err("getaddrinfo");
    }
    sstate.listen_socket = socket(sstate.obtained_addrinfo->ai_family, 
            sstate.obtained_addrinfo->ai_socktype, 
            sstate.obtained_addrinfo->ai_protocol);
    if (sstate.listen_socket == INVALID_SOCKET) {
        throw sstate.err("socket");
    }
    auto bind_result = bind(sstate.listen_socket, sstate.obtained_addrinfo->ai_addr, (int)sstate.obtained_addrinfo->ai_addrlen);
    if (bind_result == SOCKET_ERROR) {
        throw sstate.err("bind");
    }
    freeaddrinfo(sstate.obtained_addrinfo);
    sstate.obtained_addrinfo = 0;

    auto listen_result = listen(sstate.listen_socket, SOMAXCONN);
    if (listen_result == SOCKET_ERROR) {
        throw sstate.err("listen");
    }

    while (true) {
        auto client_socket = accept(sstate.listen_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET) {
            throw sstate.err("accept");
        }
        sstate.proccess_request(client_socket);
        closesocket(client_socket);
    }
}
#endif

void net::tcp_server::listen() const {
    printf("Listening on %s:%d\n", this->address, this->port);
#ifdef __unix__
    unix_listen(*this);
#elif defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    win32_listen(*this);
#else
#error "Unsupported platform"
#endif
}
