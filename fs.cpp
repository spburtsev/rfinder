#include <queue>
#include <cstring>
#include "fs.hpp"
#include "protocol.hpp"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)

#include <windows.h>
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

#include <string_view>

static std::string win32_get_error() {
    auto err_code = GetLastError();
    LPSTR buffer = 0;
    auto buf_size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        err_code,
        0,
        (LPSTR)&buffer,
        0,
        NULL
    );
    if (!buf_size) {
        return std::string("Unknown Windows error with code: ") + std::to_string(err_code);
    }
    try {
        std::string out{ buffer };
        LocalFree(buffer);
        return out;
    } catch (...) {
        LocalFree(buffer);
        throw;
    }
}

static bool win32_dir_exists(const std::string& dirname) {
    auto attrs = GetFileAttributesA(dirname.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return attrs & FILE_ATTRIBUTE_DIRECTORY;
}

struct win32_find_guard final {
    HANDLE handle;

    explicit win32_find_guard(HANDLE h) 
        : handle(h) {}

    ~win32_find_guard() {
        if (this->handle && this->handle != INVALID_HANDLE_VALUE) {
            FindClose(this->handle);
            this->handle = 0;
        }
    }
};

static std::string win32_combine_path(const std::string& dir, const char* filename) {
    char full_path[MAX_PATH];
    if (PathCombineA(full_path, dir.c_str(), filename)) {
        return std::string(full_path);
    } else {
        throw std::runtime_error("Failed to combine paths");
    }
}

static std::string win32_find_file_iter(
    std::queue<std::string>& to_visit,
    const std::string& filename
) {
    while (!to_visit.empty()) {
        auto dir_to_search = to_visit.front();
        to_visit.pop();

        WIN32_FIND_DATAA data;
        auto wildcard = win32_combine_path(dir_to_search, "*");
        auto listing = win32_find_guard(FindFirstFileA(wildcard.c_str(), &data));
        if (listing.handle == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_ACCESS_DENIED) {
                continue;
            }
            throw std::runtime_error(win32_get_error());
        }
        do {
            bool is_dir = data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
            auto name = std::string_view(data.cFileName);
            if (is_dir && name != "." && name != "..") {
                to_visit.emplace(win32_combine_path(dir_to_search, name.data()));
            } else if (data.cFileName == filename) {
                return win32_combine_path(dir_to_search, name.data());
            }
        } while (FindNextFileA(listing.handle, &data));
    }
    return "";
}

std::string fs::find_file(const proto::file_search_request& req) {
    auto root_path = req.root_path;
    if (!root_path.empty() && !win32_dir_exists(root_path)) {
        throw proto::root_dir_not_found(root_path);
    } else {
        root_path = "C:\\";
    }
    std::queue<std::string> to_visit;
    to_visit.push(root_path);
    return win32_find_file_iter(to_visit, req.filename);
}

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
#error "Unsupported platform"
#endif

