#ifndef __PROTOCOL_HPP__
#define __PROTOCOL_HPP__

#include <string>
#include <vector>

namespace proto {

    struct file_search_request final {
        std::string filename;
        std::string root_path;

        std::vector<char> serialize() const;

        static file_search_request parse_from_buffer(const char* buffer, size_t size);
    };

    enum class file_search_status {
        pending,
        ok,
        error
    };

    inline std::string to_string(file_search_status status) {
        switch (status) {
            case file_search_status::pending: return "PENDING";
            case file_search_status::ok: return "OK";
            case file_search_status::error: return "ERROR";
        }
        return "UNKNOWN";
    }

    struct file_search_response final {
        file_search_status status;
        std::string payload;

        std::vector<char> serialize() const;

        static file_search_response parse_from_buffer(const char* buffer, size_t size);
    };

} // proto

#endif // __PROTOCOL_HPP__
 
