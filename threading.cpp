#include <cassert>
#include <chrono>
#include <stdexcept>
#include "threading.hpp"
#include "fs.hpp"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)

static DWORD WINAPI send_processing_message(LPVOID args) {
    auto* handle = (threading::win32_task_handle*)args;
    auto interval = std::chrono::milliseconds(500).count();

    proto::file_search_response msg;
    msg.payload = "Processing...";
    msg.status = proto::file_search_status::pending;

    while (true) {
        if (handle->is_completed()) {
            break;
        }
        handle->callback(handle, msg);
        Sleep(interval);
    }
    return 0;
}

static void print_processing_until_completed(threading::win32_task_handle& handle) {
    DWORD thread_id;
    handle.messaging_thread_handle = CreateThread(0, 0, send_processing_message, &handle, 0, &thread_id);
    if (handle.messaging_thread_handle == 0) {
        throw std::runtime_error("CreateThread error: " + std::to_string(GetLastError()));
    }
}

static DWORD WINAPI search_for_file(LPVOID args) {
    std::unique_ptr<threading::win32_task_handle> handle((threading::win32_task_handle*)args);
    handle->completed = 0;
    proto::file_search_response res;

    auto& req = handle->req;

    try {
        std::string_view root = req.root_path;
        if (root.empty()) {
            root = "C:\\";
        } else if (!fs::dir_exists(root)) {
            res.status = proto::file_search_status::error;
            res.payload = "Invalid root path";
            handle->callback(handle.get(), res);
            return 0; 
        }
        print_processing_until_completed(*handle);
        std::string filepath = fs::find_file(req.filename, root);
        handle->end_messaging();
        res.status = proto::file_search_status::ok;
        if (filepath.empty()) {
            res.payload = "Not found";
        } else {
            res.payload = filepath;
        }
        handle->callback(handle.get(), res);
    } catch (...) {
        handle->end_messaging();
        res.status = proto::file_search_status::error;
        res.payload = "Internal error";
        handle->callback(handle.get(), res);
        throw;
    }
    return 0;
}

void threading::find_file_task(std::unique_ptr<win32_task_handle> handle) {
    DWORD thread_id;
    auto t = CreateThread(0, 0, search_for_file, handle.get(), 0, &thread_id);
    if (t == 0) {
        throw std::runtime_error("CreateThread error: " + std::to_string(GetLastError()));
    }
    handle.release();
}

#elif __unix__

#include <unistd.h>

using repeating_routine = void*(*)(void*);

static void* send_processing_message(void* args) {
    auto* handle = (threading::unix_task_handle*)args;
    __useconds_t interval = std::chrono::milliseconds(500).count() * 1000;

    proto::file_search_response msg;
    msg.payload = "Processing...";
    msg.status = proto::file_search_status::pending;

    while (true) {
        if (handle->is_completed()) {
            break;
        }
        handle->callback(handle, msg);
        usleep(interval);
    }
    return 0;
}

static pthread_t print_processing_until_completed(threading::unix_task_handle& handle) {
    pthread_t thread;
    pthread_create(&thread, 0, send_processing_message, &handle);
    if (thread == 0) {
        throw std::runtime_error("pthread_create error");
    }
    return thread;
}

static void* search_file(void* args) {
    std::unique_ptr<threading::unix_task_handle> handle((threading::unix_task_handle*)args);
    handle->completed = 0;

    proto::file_search_response res;
    auto& req = handle->req;

    try {
        handle->messaging_thread = print_processing_until_completed(*handle);
        std::string_view root = req.root_path;
        if (req.root_path.empty()) {
            root = "/";
        } else if (!fs::dir_exists(req.root_path)) {
            res.status = proto::file_search_status::error;
            res.payload = "Invalid root path";
            handle->end_messaging(res);
            return 0;
        }
        std::string filepath = fs::find_file(req.filename, root);
        res.status = proto::file_search_status::ok;
        if (filepath.empty()) {
            res.payload = "Not found";
        } else {
            res.payload = filepath;
        }
        handle->end_messaging(res);
    } catch (...) {
        res.status = proto::file_search_status::error;
        res.payload = "Internal error";
        handle->end_messaging(res);
        // TODO: find out what really happens when throwing from a detached thread
        // pthread_exit(0);
        throw;
    }
    return 0;
}

void threading::find_file_task(std::unique_ptr<unix_task_handle> handle) {
    pthread_t thread;
    pthread_create(&thread, 0, search_file, handle.get());
    if (thread == 0) {
        throw std::runtime_error("pthread_create error");
    } 
    handle.release();
    pthread_detach(thread);
}

#else
#error "Unsupported platofrm"
#endif

