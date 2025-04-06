#include <queue>
#include <string>
#include <cstring>
#include <stdexcept>
#include "fs.hpp"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)

#include <windows.h>
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

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
    std::string_view filename
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
            const auto name = std::string_view(data.cFileName);
            if (is_dir && name != "." && name != "..") {
                to_visit.emplace(win32_combine_path(dir_to_search, name.data()));
            } else if (name == filename) {
                return win32_combine_path(dir_to_search, name.data());
            }
        } while (FindNextFileA(listing.handle, &data));
    }
    return "";
}

bool fs::dir_exists(std::string_view absolute_path) noexcept {
    auto attrs = GetFileAttributesA(absolute_path.data());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return attrs & FILE_ATTRIBUTE_DIRECTORY;
}

std::string fs::find_file(std::string_view filename, std::string_view root) {
    if (root.empty()) {
        throw std::runtime_error("Empty root path is not allowed for security and cross-platform compatibility reasons.");
    }
    std::queue<std::string> to_visit;
    to_visit.push(std::string(root));
    return win32_find_file_iter(to_visit, filename);
}

#elif __unix__

#include <sys/types.h>
#include <sys/stat.h>
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
    std::string_view filename
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
                break;
            }
            if (dir_entry->d_type == DT_DIR && strcmp(dir_entry->d_name, ".") && strcmp(dir_entry->d_name, "..")) {
                to_visit.push(dir_to_search + dir_entry->d_name + '/');
                continue;
            }
            if (!strcmp(dir_entry->d_name, filename.data())) {
                return std::string(dir_to_search) + std::string(filename);
            }
        }
    }
    return "";
}

bool fs::dir_exists(std::string_view absolute_path) noexcept {
    struct stat statbuf;
    if (stat(absolute_path.data(), &statbuf) != 0) {
        return false;
    }
    return S_ISDIR(statbuf.st_mode);
}

std::string fs::find_file(std::string_view filename, std::string_view root) {
    if (root.empty()) {
        throw std::runtime_error("Empty root path is not allowed for security and cross-platform compatibility reasons.");
    }
    std::queue<std::string> to_visit;
    to_visit.push(std::string(root));
    return find_file_iter(to_visit, filename);
}

#else
#error "Unsupported platform"
#endif

