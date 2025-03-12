#include "fs.hpp"
#include "protocol.hpp"
#include <cstring>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)

static_assert(false, "Not implemented on Windows yet");

#elif __unix__

#include <sys/types.h>
#include <dirent.h>
#include <cerrno>

struct unix_dir_guard final {
    DIR* dir = 0;

    explicit unix_dir_guard(DIR* dir) 
        : dir(dir) {}

    ~unix_dir_guard() {
        if (dir) {
            closedir(dir);
        }
    }
};

bool fs::file_exisits(const proto::file_search_request &req) {
    unix_dir_guard directory {opendir(req.root_path.c_str())};
    if (!directory.dir) {
        throw fs::dir_not_found(req.root_path);
    }

    dirent* dir_entry = 0;

    while (true) {
        dir_entry = readdir(directory.dir);
        if (!dir_entry) {
            if (errno) throw std::runtime_error("Error while reading the directory entry");
            break;
        }
        if (!strcmp(dir_entry->d_name, req.filename.c_str())) {
            return true;
        }
    }
    return false;
}

#else

static_assert(false, "Unknown platform");

#endif

