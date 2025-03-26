#ifndef __FS_HPP__
#define __FS_HPP__

#include <string_view>

namespace fs {

/**
 * Finds a file by its name in a filetree, starting from the specified root and return its full path.
 * @throws std::runtime exceptions on system errors.
 * @returns empty string if file not found.
 */
std::string find_file(std::string_view filename, std::string_view root);

/**
 * Check if specified path is an existing directory
 */
bool dir_exists(std::string_view absolute_path) noexcept;

} // fs

#endif // __FS_HPP__
