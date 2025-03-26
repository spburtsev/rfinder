#include <cassert>
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
    auto task_handle = win32_task_handle(callback);
    proto::file_search_response res;

    try {
        print_processing_until_completed(task_handle);
        std::string_view root = req.root_path;
        if (root.empty()) {
            root = "C:\\";
        } else if (!fs::dir_exists(root)) {
            task_handle.end_messaging();
            res.status = proto::file_search_status::error;
            res.payload = "Invalid root path";
            task_handle.use_callback(res);
            return;
        }
        std::string filepath = fs::find_file(req.filename, root);
        task_handle.end_messaging();
        res.status = proto::file_search_status::ok;
        if (filepath.empty()) {
            res.payload = "Not found";
        } else {
            res.payload = filepath;
        }
        task_handle.use_callback(res);
    } catch (...) {
        task_handle.end_messaging();
        res.status = proto::file_search_status::error;
        res.payload = "Internal error";
        task_handle.use_callback(res);
        throw;
    }
}

#elif __unix__

#include <unistd.h>

struct task_handle final {
    pthread_mutex_t task_mutex;
    threading::message_callback callback;
    bool completed;

    inline bool is_completed() {
        pthread_mutex_t* task_mutex = &this->task_mutex;
        pthread_mutex_lock(task_mutex);
        bool result = this->completed;
        pthread_mutex_unlock(task_mutex);
        return result;
    }

    inline void mark_completed() {
        pthread_mutex_t* task_mutex = &this->task_mutex;
        pthread_mutex_lock(task_mutex);
        this->completed = true;
        pthread_mutex_unlock(task_mutex);
    }

    inline void use_callback(const proto::file_search_response& res) {
        pthread_mutex_t* task_mut = &this->task_mutex;
        pthread_mutex_lock(task_mut);
        this->callback(res);
        pthread_mutex_unlock(task_mut);
    }
};

using repeating_routine = void*(*)(void*);

static void* send_processing_message(void* args) {
    auto* handle = (task_handle*)args;
    __useconds_t interval = std::chrono::milliseconds(500).count() * 1000;

    proto::file_search_response msg;
    msg.payload = "Processing...";
    msg.status = proto::file_search_status::pending;

    while (true) {
        if (handle->is_completed()) {
            break;
        }
        handle->use_callback(msg);
        usleep(interval);
    }
    return 0;
}

static void print_processing_until_completed(task_handle& handle) {
    pthread_t thread;
    pthread_create(&thread, 0, send_processing_message, &handle);
}

void threading::find_file_task(const proto::file_search_request& req, message_callback callback) {
    task_handle handle;
    pthread_mutex_init(&handle.task_mutex, 0);
    handle.callback = callback;
    handle.completed = false;

    print_processing_until_completed(handle);

    proto::file_search_response res;

    try {
        if (!req.root_path.empty() && !fs::dir_exists(req.root_path)) {
            handle.mark_completed();
            res.status = proto::file_search_status::error;
            res.payload = "Invalid root path";
            handle.use_callback(res);
            return;
        }
        std::string filepath = fs::find_file(req);
        handle.mark_completed();
        res.status = proto::file_search_status::ok;
        if (filepath.empty()) {
            res.payload = "Not found";
        } else {
            res.payload = filepath;
        }
        handle.use_callback(res);
    } catch (...) {
        res.status = proto::file_search_status::error;
        res.payload = "Internal error";
        handle.use_callback(res);
        throw;
    }
}

#else
#error "Unsupported platofrm"
#endif

