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

std::string fs::find_file(const proto::file_search_request &req) {
    std::string dir_to_search = req.root_path;
    if (dir_to_search.empty()) {
        dir_to_search = "/";
    } else if (dir_to_search.back() != '/') {
        dir_to_search += '/';
    }

    unix_dir_guard directory {opendir(dir_to_search.c_str())};
    if (!directory.dir) {
        throw fs::dir_not_found(dir_to_search);
    }

    dirent* dir_entry = 0;

    while (true) {
        dir_entry = readdir(directory.dir);
        if (!dir_entry) {
            // TODO: Include some error details
            if (errno) throw std::runtime_error("Error while reading the directory entry");
            break;
        }
        if (!strcmp(dir_entry->d_name, req.filename.c_str())) {
            return dir_to_search + req.filename;
        }
    }
    return "";
}

#else

static_assert(false, "Unknown platform");

#endif

