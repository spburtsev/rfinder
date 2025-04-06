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

static void unix_send_response(
    int conn_fd,
    const proto::file_search_response& response
) {
    auto serialized_res = response.serialize();
    write(conn_fd, serialized_res.data(), serialized_res.size()); 
}

static proto::file_search_request unix_process_accepted(int connection_fd) {
    assert(connection_fd != -1);    
    char buffer[1024] = {0};
    int valread = read(connection_fd, buffer, sizeof(buffer));
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

static void unix_callback(
    const void* task_handle,
    const proto::file_search_response& res
) {
    const auto* handle = (threading::unix_task_handle*)task_handle;
    unix_send_response(handle->connection_fd, res);
}

static void unix_listen(const net::tcp_server& server) {
    socket_guard server_socket;

    sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    inet_pton(AF_INET, server.address, &server_address.sin_addr);
    server_address.sin_port = htons(server.port);

    if (bind(server_socket.fd, (sockaddr*)&server_address, sizeof(server_address)) == -1) {
        throw std::runtime_error("Could not bind socket: "s + strerror(errno));
    }
    if (listen(server_socket.fd, 5) != 0) { 
        throw std::runtime_error("Listen failed: "s + strerror(errno));
    } 
    fd_set read_descriptors;
    int max_fd = server_socket.fd;

    while (true) {
        FD_ZERO(&read_descriptors);
        FD_SET(server_socket.fd, &read_descriptors);

        timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        int ready_fds = select(max_fd + 1, &read_descriptors, 0, 0, &timeout);

        if (ready_fds == -1) {
            throw std::runtime_error("Select failed: "s + strerror(errno));
        }
        if (ready_fds == 0) {
            continue;
        }

        if (FD_ISSET(server_socket.fd, &read_descriptors)) {
            sockaddr_in client_address;
            socklen_t client_address_size = sizeof(client_address);
            int client_socket = accept(
                server_socket.fd, 
                (sockaddr*)&client_address,
                &client_address_size
            );
            if (client_socket == -1) {
                throw std::runtime_error("Accept failed: "s + strerror(errno));
            } 
            auto req = unix_process_accepted(client_socket);
            fprintf(stdout, "Received request: filename: \"%s\", Root path: \"%s\"\n", 
                    req.filename.c_str(), req.root_path.c_str());

            auto task_handle = std::make_unique<threading::unix_task_handle>();
            task_handle->req = std::move(req);
            task_handle->callback = unix_callback;
            task_handle->connection_fd = client_socket;
            threading::find_file_task(std::move(task_handle));
            max_fd = std::max(max_fd, client_socket);
        }
    }
}

#elif defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)

static void win32_send_response(
    const threading::win32_task_handle* handle,
    const proto::file_search_response& response
) {
    auto serialized_res = response.serialize();
    auto sent = send(handle->connection_socket, serialized_res.data(), serialized_res.size(), 0);
    if (sent == SOCKET_ERROR) {
        throw std::runtime_error("send failed with error: " + std::to_string(WSAGetLastError()));
    }
}

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
        ZeroMemory(recvbuf, sizeof(recvbuf));
        bytes_recv = recv(client_socket, recvbuf, recvbuflen, 0);
        if (bytes_recv > 0) {
            auto req = proto::file_search_request::parse_from_buffer(recvbuf, bytes_recv);
            fprintf(stdout, "Received request: filename: \"%s\", Root path: \"%s\"\n", 
                    req.filename.c_str(), req.root_path.c_str());
            auto task_handle = std::make_unique<threading::win32_task_handle>();
            task_handle->req = std::move(req);
            task_handle->callback = win32_send_response;
            task_handle->connection_socket = client_socket;
            task_handle->messaging_thread_handle = 0;
            task_handle->completed = 0;
            threading::find_file_task(std::move(task_handle));
        } else if (bytes_recv == 0) {
            printf("Connection closing...\n");
        } else  {
            throw this->err("recv");
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
