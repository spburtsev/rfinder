#include <cassert>
#include <chrono>
#include <stdexcept>
#include "threading.hpp"
#include "fs.hpp"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <windows.h>

using interlocked_flag = volatile LONG;

struct win32_task_handle final {
    threading::message_callback callback;
    CRITICAL_SECTION callback_section;
    HANDLE messaging_thread_handle;
    interlocked_flag completed;

    explicit win32_task_handle(threading::message_callback callback) 
        : callback(callback)
        , completed(false)
        , messaging_thread_handle(0) {
        InitializeCriticalSection(&this->callback_section);
    }

    ~win32_task_handle() {
        if (this->messaging_thread_handle && this->messaging_thread_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(this->messaging_thread_handle);
            this->messaging_thread_handle = 0;
        }
        DeleteCriticalSection(&this->callback_section);
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

    void use_callback(const proto::file_search_response& res) {
        EnterCriticalSection(&this->callback_section);
        try {
            this->callback(res);
        } catch (...) {
            LeaveCriticalSection(&this->callback_section);
            throw;
        }
        LeaveCriticalSection(&this->callback_section);
    }
};

using thread_routine = LPTHREAD_START_ROUTINE;

static DWORD WINAPI send_processing_message(LPVOID args) {
    auto* handle = (win32_task_handle*)args;
    auto interval = std::chrono::milliseconds(500).count();

    proto::file_search_response msg;
    msg.payload = "Processing...";
    msg.status = proto::file_search_status::pending;

    while (true) {
        if (handle->is_completed()) {
            break;
        }
        handle->use_callback(msg);
        Sleep(interval);
    }
    return 0;
}

static void print_processing_until_completed(win32_task_handle& handle) {
    DWORD thread_id;
    handle.messaging_thread_handle = CreateThread(0, 0, send_processing_message, &handle, 0, &thread_id);
    if (handle.messaging_thread_handle == 0) {
        throw std::runtime_error("CreateThread error: " + std::to_string(GetLastError()));
    }
}

void threading::find_file_task(const proto::file_search_request& req, message_callback callback) {
    auto unix_task_handle = win32_task_handle(callback);
    proto::file_search_response res;

    try {
        print_processing_until_completed(unix_task_handle);
        std::string_view root = req.root_path;
        if (root.empty()) {
            root = "C:\\";
        } else if (!fs::dir_exists(root)) {
            unix_task_handle.end_messaging();
            res.status = proto::file_search_status::error;
            res.payload = "Invalid root path";
            unix_task_handle.use_callback(res);
            return;
        }
        std::string filepath = fs::find_file(req.filename, root);
        unix_task_handle.end_messaging();
        res.status = proto::file_search_status::ok;
        if (filepath.empty()) {
            res.payload = "Not found";
        } else {
            res.payload = filepath;
        }
        unix_task_handle.use_callback(res);
    } catch (...) {
        unix_task_handle.end_messaging();
        res.status = proto::file_search_status::error;
        res.payload = "Internal error";
        unix_task_handle.use_callback(res);
        throw;
    }
}

#elif __unix__

#include <unistd.h>

struct unix_task_handle final {
    threading::message_callback callback;
    pthread_t messaging_thread;
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
        this->callback(final_response);
    }

    ~unix_task_handle() {
        assert(!this->messaging_thread);
        if (this->messaging_thread) {
            this->end_messaging();
        }
    }
};

using repeating_routine = void*(*)(void*);

static void* send_processing_message(void* args) {
    auto* handle = (unix_task_handle*)args;
    __useconds_t interval = std::chrono::milliseconds(500).count() * 1000;

    proto::file_search_response msg;
    msg.payload = "Processing...";
    msg.status = proto::file_search_status::pending;

    while (true) {
        if (handle->is_completed()) {
            break;
        }
        handle->callback(msg);
        usleep(interval);
    }
    return 0;
}

static pthread_t print_processing_until_completed(unix_task_handle& handle) {
    pthread_t thread;
    pthread_create(&thread, 0, send_processing_message, &handle);
    if (thread == 0) {
        throw std::runtime_error("pthread_create error");
    }
    return thread;
}

void threading::find_file_task(const proto::file_search_request& req, message_callback callback) {
    proto::file_search_response res;

    unix_task_handle handle;
    handle.callback = callback;
    handle.completed = 0;
    try {
        handle.messaging_thread = print_processing_until_completed(handle);
        std::string_view root = req.root_path;
        if (req.root_path.empty()) {
            root = "/";
        } else if (!fs::dir_exists(req.root_path)) {
            res.status = proto::file_search_status::error;
            res.payload = "Invalid root path";
            handle.end_messaging(res);
            return;
        }
        std::string filepath = fs::find_file(req.filename, root);
        res.status = proto::file_search_status::ok;
        if (filepath.empty()) {
            res.payload = "Not found";
        } else {
            res.payload = filepath;
        }
        handle.end_messaging(res);
    } catch (...) {
        res.status = proto::file_search_status::error;
        res.payload = "Internal error";
        handle.end_messaging(res);
        throw;
    }
}

#else
#error "Unsupported platofrm"
#endif

