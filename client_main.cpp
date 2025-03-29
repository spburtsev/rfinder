#include <cstdio>
#include <string>
#include <stdexcept>
#include <cstdint>
#include "protocol.hpp"

using namespace std::string_literals;
using namespace std::string_view_literals;

struct invalid_arg_value final : std::runtime_error {
    explicit invalid_arg_value(const char* msg)
        : std::runtime_error(msg) {}
};

struct tcp_server_info final {
    std::string address;
    int16_t port;

    static tcp_server_info parse_from_string(std::string_view str) {
        tcp_server_info info;
        size_t colon_pos = str.find(':');
        if (colon_pos == std::string_view::npos) {
            throw invalid_arg_value("Colon separating address and port not found");
        }
        info.address = str.substr(0, colon_pos);
        try {
            info.port = static_cast<int16_t>(std::stoi(str.substr(colon_pos + 1).data()));
        } catch (std::invalid_argument& e) {
            throw invalid_arg_value("Invalid port value");
        }
        return info;
    }
};

struct command_parse_error final : std::runtime_error {
    explicit command_parse_error(const char* msg)
        : std::runtime_error(msg) {}

    explicit command_parse_error(const std::string& msg)
        : std::runtime_error(msg) {}
};

struct command_options final {
    std::string file_name;
    std::string root_path;
    tcp_server_info server_info;
    int connection_timeout_seconds = 60;

    static command_options parse(int argc, char** argv) {
        command_options opts;
        static const int positional_args_num = 3;
        if (argc < positional_args_num) {
            throw command_parse_error("Not enough arguments");
        }
        // parse options
        int current_arg_idx = 1;
        while (current_arg_idx < argc) {
            char* arg = argv[current_arg_idx];
            if (arg == "-t"sv || arg == "--timeout"sv) {
                ++current_arg_idx;
                if (current_arg_idx >= argc) {
                    throw command_parse_error("Timeout option without value");
                }
                try {
                    opts.connection_timeout_seconds = std::stoi(argv[current_arg_idx]);
                } catch (std::invalid_argument& e) {
                    throw command_parse_error("Invalid timeout value");
                }
                ++current_arg_idx;
            } else {
                break;
            }
        }
        int remaining_args = argc - current_arg_idx + 1;
        if (remaining_args < positional_args_num) {
            throw command_parse_error("Not enough positional arguments");
        }
        // parse positional arguments
        try {
            opts.server_info = tcp_server_info::parse_from_string(argv[current_arg_idx]);
            ++current_arg_idx;
        } catch (const invalid_arg_value& e) {
            throw command_parse_error("Invalid address format: " + std::string(e.what()));
        }
        opts.file_name = argv[current_arg_idx];
        ++current_arg_idx;
        if (current_arg_idx < argc) {
            opts.root_path = argv[current_arg_idx];
        }
        return opts;
    }
};

static void print_usage(const char* prog_name) {
    fprintf(stdout, "Usage: %s [OPTIONS]... ADDRESS FILENAME [ROOT]\n", prog_name);
    fputs("Options:\n", stdout);
    fputs("  -t, --timeout SECONDS   Set connection timeout in seconds (default: 60)\n", stdout);
}

struct connection_error final : std::runtime_error {
    explicit connection_error(const char* msg)
        : std::runtime_error(msg) {}
};

struct invalid_server_info_error final : std::runtime_error {
    explicit invalid_server_info_error(const char* msg)
        : std::runtime_error(msg) {}
};

#ifdef __unix__
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

struct unix_connection_state final {
    int client_socket;

    unix_connection_state() {
        this->client_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (this->client_socket == -1) {
            throw std::runtime_error("Could not create socket");
        }
    }

    ~unix_connection_state() {
        if (this->client_socket != -1) {
            close(this->client_socket);
            this->client_socket = -1;
        }
    }
};

static void unix_send_request(
    const command_options& opts
) {
    const auto& server_info = opts.server_info;
    unix_connection_state cstate;
    int client_socket = cstate.client_socket;

    int flags = fcntl(client_socket, F_GETFL, 0);
    if (flags == -1) {
        throw std::runtime_error("Failed to get socket flags");
    }
    if (fcntl(client_socket, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::runtime_error("Failed to set socket to non-blocking mode");
    }

    sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    auto ptoned = inet_pton(AF_INET, server_info.address.data(), &server_address.sin_addr);
    if (ptoned == 0) {
        throw invalid_server_info_error("Invalid address");
    }
    server_address.sin_port = htons(server_info.port);
    fprintf(stdout, "Connecting to the server...\n");

    auto result = connect(client_socket, (sockaddr*)&server_address, sizeof(server_address));
    if (result == -1 && errno != EINPROGRESS) {
        throw connection_error("Could not connect to server");
    }
    auto timeout_seconds = opts.connection_timeout_seconds;
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(client_socket, &write_fds);
    timeval timeout;
    timeout.tv_sec = timeout_seconds;
    timeout.tv_usec = 0;

    result = select(client_socket + 1, nullptr, &write_fds, nullptr, &timeout);
    if (result == -1) {
        throw std::runtime_error("select() failed: " + std::string(strerror(errno)));
    } else if (result == 0) {
        throw std::runtime_error("Connection timed out after " + std::to_string(timeout_seconds) + " seconds");
    }
    int sock_error = 0;
    socklen_t len = sizeof(sock_error);
    if (getsockopt(client_socket, SOL_SOCKET, SO_ERROR, &sock_error, &len) == -1) {
        throw std::runtime_error("getsockopt() failed: " + std::string(strerror(errno)));
    }
    if (sock_error != 0) {
        throw std::runtime_error("Connection failed: " + std::string(strerror(sock_error)));
    }
    // Restore socket to blocking mode
    if (fcntl(client_socket, F_SETFL, flags) == -1) {
        throw std::runtime_error("Failed to restore socket to blocking mode");
    }
    fprintf(stdout, "Connected to the server\n");

    proto::file_search_request req;
    req.filename = opts.file_name;
    req.root_path = opts.root_path;

    auto buffer = req.serialize();
    if (send(client_socket, buffer.data(), buffer.size(), 0) == -1) {
        throw std::runtime_error("Could not send request");
    }
    while (true) {
        char res_buf[1024] = {0};
        ssize_t res_bytes = read(client_socket, res_buf, sizeof(res_buf));
        if (res_bytes == -1) {
            throw std::runtime_error("Could not read response");
        } else if(res_bytes == 0) {
            fprintf(stdout, "Connection closed by the server\n");
            break;
        }
        auto res = proto::file_search_response::parse_from_buffer(res_buf, res_bytes);
        if (res.status == proto::file_search_status::ok) {
            fprintf(stdout, "Completed with message: \"%s\"\n", res.payload.c_str());
            break;
        }
        if (res.status == proto::file_search_status::error) {
            fprintf(stdout, "Error: %s\n", res.payload.c_str());
            break;
        }
        if (res.status == proto::file_search_status::pending) {
            fprintf(stdout, "Message: %s\n", res.payload.c_str());
            continue;
        }
        fprintf(stderr, "Response with unexpected status. Payload: %s\n", res.payload.c_str());
        return;
    }
}

#else
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

struct win32_client_state final {
    WSADATA wsa_data;
    SOCKET client_socket;
    addrinfo* server_info;
    addrinfo hints;

    win32_client_state(const tcp_server_info& tcp_info) 
         : client_socket(INVALID_SOCKET), server_info(0) {
        auto ret_code = WSAStartup(MAKEWORD(2,2), &this->wsa_data);
        if (ret_code != 0) {
            throw std::runtime_error("WSAStartup failed with code: " + std::to_string(ret_code));
        }
        ZeroMemory(&this->hints, sizeof(this->hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        ret_code = getaddrinfo(
            tcp_info.address.c_str(), 
            std::to_string(tcp_info.port).c_str(), 
            &hints, 
            &server_info
        );
        if (ret_code != 0) {
            throw std::runtime_error("getaddrinfo failed with error: " + std::to_string(ret_code));
        }
    }

    ~win32_client_state() {
        if (this->client_socket != INVALID_SOCKET) {
            closesocket(this->client_socket);
            this->client_socket = INVALID_SOCKET;
        }
        if (this->server_info != 0) {
            freeaddrinfo(this->server_info);
            this->server_info = 0;
        }
        WSACleanup();
    }
};

static void win32_send_request(
    const tcp_server_info& server_info,
    const proto::file_search_request& req
) {
    win32_client_state cstate(server_info);
    for (addrinfo* addr = cstate.server_info; addr != 0; addr = addr->ai_next) {
        cstate.client_socket = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (cstate.client_socket == INVALID_SOCKET) {
            throw std::runtime_error("socket failed with error: " + std::to_string(WSAGetLastError()));
        }
        auto conn_result = connect(cstate.client_socket, addr->ai_addr, (int)addr->ai_addrlen);
        if (conn_result == SOCKET_ERROR) {
            closesocket(cstate.client_socket);
            cstate.client_socket = INVALID_SOCKET;
            continue;
        }
        break;
    }
    freeaddrinfo(cstate.server_info);
    cstate.server_info = 0;

    if (cstate.client_socket == INVALID_SOCKET) {
        throw std::runtime_error("Unable to connect to server!");
    }

    auto payload = req.serialize();
    auto socket_ret = send(cstate.client_socket, payload.data(), (int)payload.size(), 0);
    if (socket_ret == SOCKET_ERROR) {
        throw std::runtime_error("send failed with error: " + std::to_string(WSAGetLastError()));
    }
    fprintf(stdout, "Bytes Sent: %ld\n", socket_ret);

    socket_ret = shutdown(cstate.client_socket, SD_SEND);
    if (socket_ret == SOCKET_ERROR) {
        throw std::runtime_error("shutdown failed with error: " + std::to_string(WSAGetLastError()));
    }

    char recvbuf[1024];
    constexpr int recvbuflen = sizeof(recvbuf);
    do {
        socket_ret = recv(cstate.client_socket, recvbuf, recvbuflen, 0);
        if (socket_ret > 0) {
            auto res = proto::file_search_response::parse_from_buffer(recvbuf, socket_ret);
            if (res.status == proto::file_search_status::ok) {
                fprintf(stdout, "Completed with message: \"%s\"\n", res.payload.c_str());
                break;
            }
            if (res.status == proto::file_search_status::error) {
                fprintf(stdout, "Error: %s\n", res.payload.c_str());
                break;
            }
            if (res.status == proto::file_search_status::pending) {
                fprintf(stdout, "Message: %s\n", res.payload.c_str());
                continue;
            }
            fprintf(stderr, "Response with unexpected status. Payload: %s\n", res.payload.c_str());
        } else if (socket_ret == 0) {
            fprintf(stdout, "Connection closed\n");
        } else {
            fprintf(stderr, "recv failed with error: %d\n", WSAGetLastError());
        }
    } while (socket_ret > 0);
}

#endif

int main(int argc, char** argv) {
    command_options opts;
    try {
        opts = command_options::parse(argc, argv);
    } catch (const command_parse_error& e) {
        print_usage(argv[0]);
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
    
    fputs("**********\n", stdout);
    fprintf(stdout, "Server address: %s\nServer port: %d\nFilename: %s\nRoot path: %s\nConnection timeout: %ds\n", 
        opts.server_info.address.c_str(), 
        opts.server_info.port,
        opts.file_name.c_str(),
        opts.root_path.c_str(),
        opts.connection_timeout_seconds);
    fputs("**********\n\n", stdout);

    try {
#ifdef __unix__
        unix_send_request(opts);
#else 
        win32_send_request(server_info, req);
#endif
    } catch (const std::exception& e) {
        fprintf(stderr, "Error while processing request: %s\n", e.what());
    }
    return 0;
}
