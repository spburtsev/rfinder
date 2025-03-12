#ifndef __FS_HPP__
#define __FS_HPP__

#include <stdexcept>
#include "protocol.hpp"

namespace fs {

/**
 * Find file and return its full path.
 * @throws proto::root_dir_not_found if root_path does not exist.
 * @returns empty string if file not found.
 */
std::string find_file(const proto::file_search_request& req);

} // fs

#endif // __FS_HPP__