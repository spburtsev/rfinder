#include "networking.hpp"
#include "threading.hpp"

static proto::file_seach_response handle_request(const proto::file_search_request& req) {
    proto::file_seach_response response;
    try {
        auto filepath = threading::find_file_task(req);
        if (filepath.empty()) {
            response.status = proto::file_search_status::NOT_FOUND;
        } else {
            response.status = proto::file_search_status::FOUND;
            response.payload = filepath;
        }
    } catch (const proto::root_dir_not_found& ex) {
        response.status = proto::file_search_status::ERROR;
        response.payload = "Specified root directory not found: " + std::string(ex.what());
    } catch (const std::exception& ex) {
        response.status = proto::file_search_status::ERROR;
        response.payload = "Internal error";
        fprintf(stderr, "Unexpected error: %s\n", ex.what());
    }
    return response;
}

void net::listen(const server& server) {
    printf("Listening on %s:%d\n", server.address, server.port);

    // simulate a file search request
    proto::file_search_request req;
    req.filename = "server.cpp";
    req.root_path = "/";
    auto response = handle_request(req);
    printf("Response:\nStatus: %s\nPayload: %s\n", 
        proto::to_string(response.status).c_str(),
        response.payload.c_str());
}
