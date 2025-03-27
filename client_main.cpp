#include <cstdio>
#include <cstring>
#include <cstdint>
#include <stdexcept>

#include "protocol.hpp"

struct tcp_server_info final {
    std::string address;
    int16_t port;
};

static void rtrim(std::string& str, char c) {
    while (!str.empty() && str.back() == c) {
        str.pop_back();
    }
}

static std::string read_input(const char* prompt) {
    char input[256] = {0};
    fputs(prompt, stdout);

    while (true) {
        if (fgets(input, sizeof(input), stdin) == 0) {
            fprintf(stderr, "Could not read input\n");
            memset(input, 0, sizeof(input));
            continue;
        }
        std::string input_str(input);
        rtrim(input_str, '\n');
        return input_str;
    }
}

#ifdef __unix__
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

static void unix_send_request(
    const tcp_server_info& server_info,
    const proto::file_search_request& req
) {
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        throw std::runtime_error("Could not create socket");
    }

    sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    inet_pton(AF_INET, server_info.address.c_str(), &server_address.sin_addr);
    server_address.sin_port = htons(server_info.port);

    if (connect(client_socket, (sockaddr*)&server_address, sizeof(server_address)) == -1) {
        throw std::runtime_error("Could not connect to server");
    }

    auto buffer = req.serialize();
    if (send(client_socket, buffer.data(), buffer.size(), 0) == -1) {
        close(client_socket);
        throw std::runtime_error("Could not send request");
    }
    while (true) {
        char res_buf[1024] = {0};
        ssize_t res_bytes = read(client_socket, res_buf, sizeof(res_buf));

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
        close(client_socket);
        return;
    }
    close(client_socket);
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
    tcp_server_info server_info = {};
    server_info.address = read_input("Server address: ");
    server_info.port = std::stoi(read_input("Server port: "));
    
    fputs("**********\n", stdout);
    fprintf(stdout, "Server address: %s\nServer port: %d\n", server_info.address.c_str(), server_info.port);
    fputs("**********\n\n", stdout);

    proto::file_search_request req;

    while (true) {
        auto input = read_input("Filename or 'exit': ");
        if (input == "exit") {
            return 0;
        }
        if (input.empty()) {
            fputs("Filename cannot be empty\n", stdout);
            continue;
        }
        req.filename = input;

        input = read_input("Root path (optional): ");
        req.root_path = input;
        try {
#ifdef __unix__
            unix_send_request(server_info, req);
#else 
            win32_send_request(server_info, req);
#endif
        } catch (const std::exception& e) {
            fprintf(stderr, "Error while sending request: %s\n", e.what());
        }
    }

    return 0;
}
