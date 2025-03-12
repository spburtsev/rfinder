#include <exception>
#include <cstdio>

#include "protocol.hpp"
#include "fs.hpp"

int main() {
    proto::file_search_request req;
    req.filename = "server.cpp";
    req.root_path = "/home/spburtsev/personal/rfinder/";

    try {
        auto filepath = fs::find_file(req);
        if (filepath.empty()) {
            fprintf(stderr, "File not found.\n");
            return 1;
        }
        printf("File found: %s.\n", filepath.c_str());
    } catch (const std::exception& ex) {
        fprintf(stderr, "Error: %s\n", ex.what());
        return -1;
    }
    return 0;
}
