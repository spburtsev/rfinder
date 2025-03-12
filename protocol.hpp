#ifndef __PROTOCOL_HPP__
#define __PROTOCOL_HPP__

#include <string>
#include <cstdint>

namespace proto {

struct file_search_request final {
    std::string filename;
    std::string root_path;
};

struct file_seach_response final {
    uint32_t found = 0; 
};

} // proto

#endif // __PROTOCOL_HPP__
 
