#include "threading.hpp"
#include "fs.hpp"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)

static_assert(false, "Not implemented on Windows yet");

#elif __unix__

#include <unistd.h>

struct task_completion_handle {
    bool completed;
    pthread_mutex_t mutex;
};

struct print_processing_args {
    task_completion_handle* handle;
    std::chrono::milliseconds interval;
};

using repeating_routine = void*(*)(void*);

static bool is_completed(task_completion_handle& handle) {
    pthread_mutex_t* mutex = &handle.mutex;
    pthread_mutex_lock(mutex);
    bool result = handle.completed;
    pthread_mutex_unlock(mutex);
    return result;
}

static void mark_completed(task_completion_handle& handle) {
    pthread_mutex_t* mutex = &handle.mutex;
    pthread_mutex_lock(mutex);
    handle.completed = true;
    pthread_mutex_unlock(mutex);
}

static void* print_processing(void* args) {
    auto* print_args = (print_processing_args*)args;
    while (true) {
        if (is_completed(*print_args->handle)) {
            break;
        }
        std::cout << "Processing..." << std::endl;
        __useconds_t interval = print_args->interval.count() * 1000;
        usleep(interval);
    }
    return 0;
}

static void print_processing_until_completed(task_completion_handle& handle) {
    pthread_t thread;
    print_processing_args args = { &handle, std::chrono::milliseconds(500) };
    pthread_create(&thread, 0, print_processing, &args);
}

std::string threading::find_file_task(const proto::file_search_request& req) {
    task_completion_handle handle = {0};
    pthread_mutex_init(&handle.mutex, 0);
    print_processing_until_completed(handle);

    try {
        std::string result = fs::find_file(req);
        mark_completed(handle);
        return result;
    } catch (...) {
        mark_completed(handle);
        throw;
    }
}

#else

static_assert(false, "Unknown platform");

#endif

