#ifndef __THREADING_HPP__
#define __THREADING_HPP__

#include <functional>
#include <chrono>
#include <iostream>

#include "protocol.hpp"

namespace threading {

    std::string find_file_task(const proto::file_search_request& req);

} // threading

#endif // __THREADING_HPP__