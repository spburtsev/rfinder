#ifndef __PROTOCOL_HPP__
#define __PROTOCOL_HPP__

#include <string>

namespace proto {

struct file_search_request final {
    std::string filename;
    std::string root_path;
};

struct file_seach_response final {
    std::string full_path;
};

} // proto

#endif // __PROTOCOL_HPP__
 
