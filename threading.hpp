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
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment (lib, "Ws2_32.lib")

namespace threading {

    struct win32_task_handle final {
        proto::file_search_request req;
        std::function<void(const win32_task_handle*, const proto::file_search_response&)> callback;
        SOCKET connection_socket;
        HANDLE messaging_thread_handle;
        volatile LONG completed;

        ~win32_task_handle() {
            if (this->messaging_thread_handle && this->messaging_thread_handle != INVALID_HANDLE_VALUE) {
                this->end_messaging();
                CloseHandle(this->messaging_thread_handle);
                this->messaging_thread_handle = 0;
            }
            shutdown(this->connection_socket, SD_SEND);
            closesocket(this->connection_socket);
            this->connection_socket = INVALID_SOCKET;
        }

        bool is_completed() {
            return _InterlockedOr(&this->completed, 0) != 0;
        }

        void mark_completed() {
            _InterlockedExchange(&this->completed, 1);
        }

        void end_messaging() {
            this->mark_completed();
            assert(this->messaging_thread_handle && this->messaging_thread_handle != INVALID_HANDLE_VALUE);
            WaitForSingleObject(this->messaging_thread_handle, INFINITE);
        }
    };

    void find_file_task(std::unique_ptr<win32_task_handle> handle);

} // threading
#endif

#endif // __THREADING_HPP__
