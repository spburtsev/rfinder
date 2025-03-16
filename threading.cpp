#include "threading.hpp"
#include "fs.hpp"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#error "Unsupported platform"
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

    inline void use_callback(const proto::file_seach_response& res) {
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

    #ifdef DEBUG
    fprintf(stdout, "Interval is %d microseconds\n", interval);
    #endif

    proto::file_seach_response msg;
    msg.payload = "Processing...";
    msg.status = proto::file_search_status::PENDING;

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

    proto::file_seach_response res;

    try {
        std::string filepath = fs::find_file(req);
        handle.mark_completed();
        res.status = proto::file_search_status::OK;
        if (filepath.empty()) {
            res.payload = "Not found";
        } else {
            res.payload = filepath;
        }
        handle.use_callback(res);
    } catch (const proto::root_dir_not_found& ex) {
        res.status = proto::file_search_status::ERROR;
        res.payload = std::string(ex.what());
        handle.use_callback(res);
    } catch (const std::exception& ex) {
        res.status = proto::file_search_status::ERROR;
        res.payload = "Internal error";
        handle.use_callback(res);
        throw ex;
    }
}

#else
#error "Unsupported platofrm"
#endif

