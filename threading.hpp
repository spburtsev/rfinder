#ifndef __THREADING_HPP__
#define __THREADING_HPP__

#include <functional>
#include <memory>
#include "protocol.hpp"


#ifdef __unix__
#include <unistd.h>

namespace threading {

    using message_callback = std::function<void(
        const void* connection_handle,
        const proto::file_search_response& response
    )>;

    struct unix_task_handle final {
        proto::file_search_request req;
        message_callback callback;
        pthread_t messaging_thread;
        int connection_fd;
        volatile int completed;

        bool is_completed() {
            return __atomic_load_n(&this->completed, __ATOMIC_ACQUIRE);
        }

        void end_messaging() {
            __atomic_store_n(&this->completed, 1, __ATOMIC_RELEASE);
            pthread_join(this->messaging_thread, 0);
            this->messaging_thread = 0;
        }

        void end_messaging(const proto::file_search_response& final_response) {
            this->end_messaging();
            this->callback(this, final_response);
        }

        ~unix_task_handle() {
            if (this->messaging_thread) {
                this->end_messaging();
            }
            if (this->connection_fd != -1) {
                close(this->connection_fd);
                this->connection_fd = -1;
            }
        }
    };
    
    void find_file_task(std::unique_ptr<unix_task_handle> handle);
} // threading

#elif defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
namespace threading {
    void find_file_task(const proto::file_search_request& req, const message_callback& callback);
} // threading
#endif

#endif // __THREADING_HPP__
