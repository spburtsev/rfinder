#include <cstdio>
#include <cstring>
#include <cstdint>

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
        fprintf(stdout, "Read input: %s\n", input_str.c_str());
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
        if (res.status == proto::file_search_status::OK) {
            fprintf(stdout, "File found: \"%s\"\n", res.payload.c_str());
            break;
        }
        if (res.status == proto::file_search_status::ERROR) {
            fprintf(stdout, "Error: %s\n", res.payload.c_str());
            break;
        }
        if (res.status == proto::file_search_status::PENDING) {
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
#error "Unsupported platform"
#endif

int main(int argc, char** argv) {
    tcp_server_info server_info = {};
    // server_info.address = read_input("Server address: ");
    // server_info.port = std::stoi(read_input("Server port: "));
    server_info.address = "127.0.0.1";
    server_info.port = 8080;
    
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
            fputs("Filename cannot be empty\n", stderr);
            continue;
        }
        req.filename = input;

        input = read_input("Root path (optional): ");
        req.root_path = input;

        #ifdef __unix__
        unix_send_request(server_info, req);
        #else 
        #error "Unsupported platform"
        #endif
    }

    return 0;
}
