#include <queue>
#include <cstring>
#include "fs.hpp"
#include "protocol.hpp"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)

#error "Not implemented on Windows yet"

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

static std::string find_file_iter(
    std::queue<std::string>& to_visit,
    const std::string& filename
) {
    while (!to_visit.empty()) {
        std::string dir_to_search = to_visit.front();
        to_visit.pop();

        #ifdef DEBUG
        // printf("Searching in %s\n", dir_to_search.c_str());
        #endif

        unix_dir_guard directory {opendir(dir_to_search.c_str())};
        if (!directory.dir) {
            continue;
        }
        
        dirent* dir_entry = 0;
        while (true) {
            dir_entry = readdir(directory.dir);
            if (!dir_entry) {
                if (errno && errno != EACCES) {
                    throw std::runtime_error("Could not read directory stream " + dir_to_search + ". " + strerror(errno));
                }
                break;
            }
            if (dir_entry->d_type == DT_DIR && strcmp(dir_entry->d_name, ".") && strcmp(dir_entry->d_name, "..")) {
                to_visit.push(dir_to_search + dir_entry->d_name + '/');
                continue;
            }
            if (!strcmp(dir_entry->d_name, filename.c_str())) {
                return dir_to_search + filename;
            }
        }
    }
    return "";
}

std::string fs::find_file(const proto::file_search_request& req) {
    std::string dir_to_search = req.root_path;
    if (dir_to_search.empty()) {
        dir_to_search = "/";
    } else if (dir_to_search.back() != '/') {
        dir_to_search += '/';
    }

    DIR* directory {opendir(dir_to_search.c_str())};
    if (!directory) {
        if (errno == ENOENT) {
            throw proto::root_dir_not_found(dir_to_search);
        }
        throw std::runtime_error("Could not open directory stream " + dir_to_search + ". " + strerror(errno));
    }
    closedir(directory);

    std::queue<std::string> to_visit;
    to_visit.push(dir_to_search);
    return find_file_iter(to_visit, req.filename);
}

#else

static_assert(false, "Unknown platform");

#endif

