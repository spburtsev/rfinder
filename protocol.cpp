#include <string.h>
#include "protocol.hpp"

static std::string generate_request_id() {
    static int id = 0;
    return std::to_string(id++);
}

std::vector<char> proto::file_search_request::serialize(const file_search_request& req) {
    std::vector<char> buffer;
    uint32_t payload_size = sizeof(uint32_t)*3 + req.filename.size() + req.root_path.size();
    buffer.reserve(payload_size);

    char* payload_size_ptr = (char*)&payload_size;
    buffer.insert(buffer.end(), payload_size_ptr, payload_size_ptr + sizeof(payload_size));

    size_t filename_size = req.filename.size();
    char* filename_size_ptr = (char*)&filename_size;
    buffer.insert(buffer.end(), filename_size_ptr, filename_size_ptr + sizeof(filename_size));
    buffer.insert(buffer.end(), req.filename.begin(), req.filename.end());

    size_t root_path_size = req.root_path.size();
    char* root_path_size_ptr = (char*)&root_path_size;
    buffer.insert(buffer.end(), root_path_size_ptr, root_path_size_ptr + sizeof(root_path_size));
    buffer.insert(buffer.end(), req.root_path.begin(), req.root_path.end());

    return buffer;
}

auto proto::file_search_request::parse_from_buffer(
    const char* buffer,
    size_t buffer_size
) -> file_search_request {
    file_search_request req;

    req.id = generate_request_id();
    uint32_t payload_size = *(uint32_t*)buffer;
    buffer += sizeof(payload_size);

    if (payload_size > buffer_size) {
        throw std::runtime_error("Invalid buffer size");
    }

    uint32_t filename_len = *(uint32_t*)buffer;
    buffer += sizeof(filename_len);
    req.filename = std::string(buffer, filename_len);
    buffer += filename_len;
    uint32_t root_path_len = *(uint32_t*)buffer;
    buffer += sizeof(root_path_len);
    req.root_path = std::string(buffer, root_path_len);
    return req;
}