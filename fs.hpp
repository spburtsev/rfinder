#include <stdexcept>
#include "protocol.hpp"

namespace fs {

struct dir_not_found final : std::runtime_error {
    explicit dir_not_found(const std::string& dirname) 
        : std::runtime_error("Directory not found: \"" + dirname + "\"") {}
};

/**
 * Find file and return its full path.
 * @throws fs::dir_not_found if root_path does not exist.
 * @returns empty string if file not found.
 */
std::string find_file(const proto::file_search_request& req);

} // fs

