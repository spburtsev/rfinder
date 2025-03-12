#include <exception>
#include <cstdio>

#include "protocol.hpp"
#include "fs.hpp"

static proto::file_seach_response process(const proto::file_search_request& req) {
    proto::file_seach_response res;
    res.found = fs::file_exisits(req) ? 1 : 0;
    return res;
}

int main() {
    proto::file_search_request req;
    req.filename = "server.cpp";
    req.root_path = "/home/spburtsev/personal/rfinder/";

    try {
        auto response = process(req);
        printf("File found: %d.\n", response.found);
    } catch (const std::exception& ex) {
        fprintf(stderr, "Error: %s\n", ex.what());
        return -1;
    }
    return 0;
}
