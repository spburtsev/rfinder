#include <stdexcept>
#include "protocol.hpp"

namespace fs {

bool file_exisits(const proto::file_search_request& req);

struct dir_not_found final : std::runtime_error {
    explicit dir_not_found(const std::string& dirname) 
        : std::runtime_error("Directory not found: \"" + dirname + "\"") {}
};

} // fs

