#ifndef __THREADING_HPP__
#define __THREADING_HPP__

#include <functional>
#include <chrono>
#include <iostream>

#include "protocol.hpp"

namespace threading {
    using message_callback = std::function<void(const proto::file_seach_response&)>;

    void find_file_task(const proto::file_search_request& req, message_callback callback);

} // threading

#endif // __THREADING_HPP__