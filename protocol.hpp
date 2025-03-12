#ifndef __PROTOCOL_HPP__
#define __PROTOCOL_HPP__

#include <string>
#include <stdexcept>

namespace proto {

struct file_search_request final {
    std::string filename;
    std::string root_path;
};

struct file_seach_response final {
    std::string full_path;
};

struct root_dir_not_found final : std::runtime_error {
    explicit root_dir_not_found(const std::string& dirname) 
        : std::runtime_error("Specified root directory does not exist: \"" + dirname + "\"") {}
};

} // proto

#endif // __PROTOCOL_HPP__
 
