#include "networking.hpp"
#include "threading.hpp"

static void send_response(const proto::file_seach_response& response) {
    printf("Response:\n  Status: %s\n  Payload: %s\n\n", 
        proto::to_string(response.status).c_str(),
        response.payload.c_str());
}

static void handle_request(const proto::file_search_request& req) {
    printf("Received request: ID: %s, Filename: %s, Root path: %s\n", 
        req.id.c_str(), req.filename.c_str(), req.root_path.c_str());

    proto::file_seach_response response;
    try {
        auto filepath = threading::find_file_task(req);
        if (filepath.empty()) {
            response.status = proto::file_search_status::NOT_FOUND;
        } else {
            response.status = proto::file_search_status::FOUND;
            response.payload = filepath;
        }
        send_response(response);
    } catch (const proto::root_dir_not_found& ex) {
        response.status = proto::file_search_status::ERROR;
        response.payload = "Specified root directory not found: " + std::string(ex.what());
        send_response(response);
    } catch (const std::exception& ex) {
        response.status = proto::file_search_status::ERROR;
        response.payload = "Internal error";
        send_response(response);
        fprintf(stderr, "Request ID: %s\nUnhandled error: %s\n", req.id.c_str(), ex.what());
        exit(1);
    }
}

#ifdef __unix__

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

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


static int unix_listen(const net::tcp_server& server) {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        throw std::runtime_error("Could not create socket");
    }
    sockaddr_in server_address = {0};
    server_address.sin_family = AF_INET;
    inet_pton(AF_INET, server.address, &server_address.sin_addr);
    server_address.sin_port = htons(server.port);

    if (bind(server_socket, (sockaddr*)&server_address, sizeof(server_address)) == -1) {
        throw std::runtime_error("Could not bind socket");
    }
    if ((listen(server_socket, 5)) != 0) { 
        throw std::runtime_error("Listen failed");
    } 
    while (true) {
        auto req = unix_receive_request(server_socket);
        handle_request(req);
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
