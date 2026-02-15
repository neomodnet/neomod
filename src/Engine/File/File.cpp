//========== Copyright (c) 2012, PG & 2025, WH, All rights reserved. ============//
//
// purpose:		file wrapper, for cross-platform unicode path support
//
// $NoKeywords: $file
//===============================================================================//

#include "File.h"
#include "ConVar.h"
#include "Engine.h"
#include "UString.h"
#include "Hashing.h"
#include "Logging.h"
#include "SyncMutex.h"
#include "SyncOnce.h"

#define WANT_PDQSORT
#include "Sorting.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <utility>
#include <vector>
#include <cstdio>

namespace fs = std::filesystem;
namespace chrono = std::chrono;

namespace {  // static namespace
//------------------------------------------------------------------------------
// encapsulation of directory caching logic
//------------------------------------------------------------------------------
class DirectoryCache final {
   public:
    DirectoryCache() = default;

    // directory entry type
    struct DirectoryEntry {
        Hash::unstable_ncase_stringmap<std::pair<std::string, File::FILETYPE>> files;
        chrono::steady_clock::time_point lastCacheAccess;
        fs::file_time_type lastModified;
    };

    // look up a file with case-insensitive matching
    std::pair<std::string, File::FILETYPE> lookup(const fs::path &dirPath, std::string_view filename) {
        std::string dirKey(dirPath.string());
        auto it = this->cache.find(dirKey);

        DirectoryEntry *entry = nullptr;

        // check if cache exists and is still valid
        if(it != this->cache.end()) {
            // check if directory has been modified
            std::error_code ec;
            auto currentModTime = fs::last_write_time(dirPath, ec);

            if(!ec && currentModTime != it->second.lastModified)
                this->cache.erase(it);  // cache is stale, remove it
            else
                entry = &it->second;
        }

        // create new entry if needed
        if(!entry) {
            // evict old entries if we're at capacity
            if(this->cache.size() >= DIR_CACHE_MAX_ENTRIES) evictOldEntries();

            // build new cache entry
            DirectoryEntry newEntry;
            newEntry.lastCacheAccess = chrono::steady_clock::now();

            std::error_code ec;
            newEntry.lastModified = fs::last_write_time(dirPath, ec);

            // scan directory and populate cache
            for(const auto &dirEntry : fs::directory_iterator(dirPath, ec)) {
                if(ec) break;

                std::string filename(dirEntry.path().filename().string());
                File::FILETYPE type = File::FILETYPE::OTHER;

                if(dirEntry.is_regular_file())
                    type = File::FILETYPE::FILE;
                else if(dirEntry.is_directory())
                    type = File::FILETYPE::FOLDER;

                // store both the actual filename and its type
                newEntry.files[filename] = {filename, type};
            }

            // insert into cache
            auto [insertIt, inserted] = this->cache.emplace(dirKey, std::move(newEntry));
            entry = inserted ? &insertIt->second : nullptr;
        }

        if(!entry) return {{}, File::FILETYPE::NONE};

        // update last access time
        entry->lastCacheAccess = chrono::steady_clock::now();

        // find the case-insensitive match
        auto fileIt = entry->files.find(filename);
        if(fileIt != entry->files.end()) return fileIt->second;

        return {{}, File::FILETYPE::NONE};
    }

   private:
    static constexpr uSz DIR_CACHE_MAX_ENTRIES = 1000;
    static constexpr uSz DIR_CACHE_EVICT_COUNT = DIR_CACHE_MAX_ENTRIES / 4;

    // evict least recently used entries when cache is full
    void evictOldEntries() {
        const uSz entriesToRemove = std::min(DIR_CACHE_EVICT_COUNT, this->cache.size());

        if(entriesToRemove == this->cache.size()) {
            this->cache.clear();
            return;
        }

        // collect entries with their access times
        std::vector<std::pair<chrono::steady_clock::time_point, decltype(this->cache)::iterator>> entries;
        entries.reserve(this->cache.size());

        for(auto it = this->cache.begin(); it != this->cache.end(); ++it)
            entries.emplace_back(it->second.lastCacheAccess, it);

        // sort by access time (oldest first)
        srt::pdqsort(entries, [](const auto &a, const auto &b) { return a.first < b.first; });

        // remove the oldest entries
        for(uSz i = 0; i < entriesToRemove; ++i) this->cache.erase(entries[i].second);
    }

    // cache storage
    Hash::unstable_stringmap<DirectoryEntry> cache;
};

// init static directory cache
// NOTE: this is only actually used outside of windows, should be optimized out in other cases
[[maybe_unused]] DirectoryCache &getDirectoryCache() {
    // NOTE: thread-local to avoid off-thread resource loading indirectly blocking the main thread, waiting to lock a mutex on the cache
    // not optimal, but does the job.
    static thread_local DirectoryCache s_directoryCache{};
    return s_directoryCache;
}

}  // namespace

//------------------------------------------------------------------------------
// directory queries
//------------------------------------------------------------------------------

#if (defined(MCENGINE_PLATFORM_LINUX) && defined(_GNU_SOURCE)) || \
    ((defined(_POSIX_C_SOURCE) && (_POSIX_C_SOURCE >= 200809L)) || defined(_ATFILE_SOURCE))

#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>

bool File::getDirectoryEntries(const std::string &pathToEnum, bool wantDirectories,
                               std::vector<std::string> &utf8NamesOut) noexcept {
    const int fd = openat64(AT_FDCWD, pathToEnum.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    if(fd == -1) {
        debugLog("openat64 failed on {}: {}", pathToEnum, strerror(errno));
        return false;
    }

    DIR *dir = fdopendir(fd);
    if(!dir) {
        debugLog("fdopendir failed on {}: {}", pathToEnum, strerror(errno));
        close(fd);
        return false;
    }

    utf8NamesOut.reserve(512);

    struct dirent64 *entry;
    while((entry = readdir64(dir)) != nullptr) {
        const char *name = &entry->d_name[0];

        // skip . and .. for directories
        if(wantDirectories && name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }

        // d_type is supported on most linux filesystems (ext4, xfs, btrfs, etc)
        // but may be DT_UNKNOWN on network filesystems
        bool is_dir;
        if(entry->d_type != DT_UNKNOWN) {
            is_dir = (entry->d_type == DT_DIR);
        } else {
            // fallback for filesystems that don't populate d_type
            struct stat64 st;
            is_dir = (fstatat64(fd, name, &st, 0) == 0 && S_ISDIR(st.st_mode));
        }

        if(wantDirectories == is_dir) {
            utf8NamesOut.emplace_back(name);
        }
    }

    closedir(dir);  // also closes fd

    return true;
}

#elif defined(MCENGINE_PLATFORM_WINDOWS)  // the win32 api is just WAY faster for this than std::filesystem
#include "WinDebloatDefs.h"
#include <fileapi.h>

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR) - 1)
#endif

bool File::getDirectoryEntries(const std::string &pathToEnum, bool wantDirectories,
                               std::vector<std::string> &utf8NamesOut) noexcept {
    // Since we want to avoid wide strings in the codebase as much as possible,
    // we convert wide paths to UTF-8 (as they fucking should be).
    // We can't just use FindFirstFileA, because then any path with unicode
    // characters will fail to open!
    // Keep in mind that windows can't handle the way too modern 1993 UTF-8, so
    // you have to use std::filesystem::u8path() or convert it back to a wstring
    // before using the windows API.

    UString folder{pathToEnum};
    // TODO: avoid this fragile bs
    UString endSep{US_("/")};
    UString otherSep{US_("\\")};
    // for UNC/long paths, make sure we use a backslash as the last separator
    if(folder.startsWith(US_(R"(\\?\)")) || folder.startsWith(US_(R"(\\.\)"))) {
        endSep = US_("\\");
        otherSep = US_("/");
    }
    if(!folder.endsWith(endSep)) {
        if(folder.endsWith(otherSep)) {
            folder.pop_back();
        }
        folder.append(endSep);
    }
    folder.append(US_("*.*"));

    WIN32_FIND_DATAW data{};
    HANDLE handle = FindFirstFileW(folder.wchar_str(), &data);
    if(handle != INVALID_HANDLE_VALUE) {
        utf8NamesOut.reserve(512);

        do {
            const wchar_t *wide_filename = &data.cFileName[0];
            const size_t length = std::wcslen(wide_filename);
            if(length == 0) continue;

            const bool add_entry =
                (!wantDirectories ||
                 !(wide_filename[0] == L'.' &&
                   (wide_filename[1] == L'\0' || (wide_filename[1] == L'.' && wide_filename[2] == L'\0')))) &&
                (wantDirectories == !!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY));

            if(add_entry) {
                UString uFilename{wide_filename, static_cast<int>(length)};
                utf8NamesOut.emplace_back(uFilename.utf8View());
            }
        } while(FindNextFileW(handle, &data));

        FindClose(handle);
    }

    return true;
}

#else

// for getting files in folder/ folders in folder
bool File::getDirectoryEntries(const std::string &pathToEnum, bool wantDirectories,
                               std::vector<std::string> &utf8NamesOut) noexcept {
    utf8NamesOut.reserve(512);

    std::error_code ec;
    const bool wantFiles = !wantDirectories;

    for(const auto &entry : fs::directory_iterator(pathToEnum, ec)) {
        if(ec) continue;
        auto fileType = entry.status(ec).type();

        if((wantFiles && fileType == fs::file_type::regular) ||
           (wantDirectories && fileType == fs::file_type::directory)) {
            utf8NamesOut.emplace_back(entry.path().filename().generic_string());
        }
    }

    if(ec && utf8NamesOut.empty()) {
        debugLog("Failed to enumerate directory: {}", ec.message());
        return false;
    }

    return true;
}

#endif  // MCENGINE_PLATFORM_WINDOWS

//------------------------------------------------------------------------------
// path resolution methods
//------------------------------------------------------------------------------
// public static
File::FILETYPE File::existsCaseInsensitive(std::string &filePath) {
    if(filePath.empty()) return FILETYPE::NONE;

    auto fsPath = getFsPath(filePath);
    return File::existsCaseInsensitive(filePath, fsPath);
}

File::FILETYPE File::exists(std::string_view filePath) {
    if(filePath.empty()) return FILETYPE::NONE;

    return File::exists(filePath, File::getFsPath(filePath));
}

// private (cache the fs::path)
File::FILETYPE File::existsCaseInsensitive(std::string &filePath, fs::path &path) {
    // windows is already case insensitive
    if constexpr(Env::cfg(OS::WINDOWS)) {
        return File::exists(filePath, path);
    }

    auto retType = File::exists(filePath, path);

    if(retType == File::FILETYPE::NONE)
        return File::FILETYPE::NONE;
    else if(!(retType == File::FILETYPE::MAYBE_INSENSITIVE))
        return retType;  // direct match

    auto parentPath = path.parent_path();

    // verify parent directory exists
    std::error_code ec;
    auto parentStatus = fs::status(parentPath, ec);
    if(ec || parentStatus.type() != fs::file_type::directory) return File::FILETYPE::NONE;

    // try case-insensitive lookup using cache
    auto [resolvedName, fileType] =
        getDirectoryCache().lookup(parentPath, {path.filename().string()});  // takes the bare filename

    if(fileType == File::FILETYPE::NONE) return File::FILETYPE::NONE;  // no match, even case-insensitively

    std::string resolvedPath(parentPath.string());
    if(!(resolvedPath.back() == '/') && !(resolvedPath.back() == '\\')) resolvedPath.push_back('/');
    resolvedPath.append(resolvedName);

    logIfCV(debug_file, "File: Case-insensitive match found for {:s} -> {:s}", path.string(), resolvedPath);

    // now update the input path reference with the actual found path
    filePath = resolvedPath;
    path = fs::path(resolvedPath);
    return fileType;
}

File::FILETYPE File::exists(std::string_view filePath, const fs::path &path) {
    if(filePath.empty()) return File::FILETYPE::NONE;

    std::error_code ec;
    auto status = fs::status(path, ec);

    if(ec || status.type() == fs::file_type::not_found)
        return File::FILETYPE::MAYBE_INSENSITIVE;  // path not found, try case-insensitive lookup

    if(status.type() == fs::file_type::regular)
        return File::FILETYPE::FILE;
    else if(status.type() == fs::file_type::directory)
        return File::FILETYPE::FOLDER;
    else
        return File::FILETYPE::OTHER;
}

void File::normalizeSlashes(std::string &str, unsigned char oldSlash, unsigned char newSlash) noexcept {
    std::ranges::replace(str, oldSlash, newSlash);

    bool prev = false;
    std::erase_if(str, [&](unsigned char c) {
        const bool remove = prev && c == newSlash;
        prev = (c == newSlash);
        return remove;
    });
}

#ifdef MCENGINE_PLATFORM_WINDOWS
#include "dynutils.h"
#include "RuntimePlatform.h"

#include "WinDebloatDefs.h"
#include <fileapi.h>
#include <libloaderapi.h>
#include <heapapi.h>
#include <errhandlingapi.h>

#include <string>

namespace {
Sync::once_flag long_path_check;
bool std_filesystem_supports_long_paths{true};

using wine_get_dos_file_name_t = LPWSTR CDECL(LPCSTR);
wine_get_dos_file_name_t *pwine_get_dos_file_name{nullptr};
bool tried_load_wine_func{false};

forceinline UString adjustPath_(std::string_view filepath) noexcept {
    static constexpr const std::string_view extPrefix{R"(\\?\)"};
    static constexpr const std::string_view devicePrefix{R"(\\.\)"};
    static constexpr const std::string_view extUncPrefix{R"(\\?\UNC\)"};
    static constexpr const std::string_view uncStart{R"(\\)"};

    if(filepath.empty()) return UString{};

    // Handle Unix absolute paths under Wine
    if(filepath.size() >= 1 && filepath[0] == '/' && (filepath.size() < 2 || filepath[1] != '/')) {
        if(pwine_get_dos_file_name ||
           (!tried_load_wine_func && (RuntimePlatform::current() & RuntimePlatform::WIN_WINE) &&
            (pwine_get_dos_file_name = dynutils::load_func<wine_get_dos_file_name_t>(
                 reinterpret_cast<dynutils::lib_obj *>(GetModuleHandleA("kernel32.dll")), "wine_get_dos_file_name")))) {
            std::string path{filepath};
            File::normalizeSlashes(path, '\\', '/');  // normalize any mixed slashes in the path portion

            LPWSTR dosPath = pwine_get_dos_file_name(path.c_str());  // use original with forward slashes
            if(dosPath) {
                UString result{dosPath, static_cast<int>(std::wcslen(dosPath))};
                // not sure if this is necessary or works as intended...
                // but don't return an extended path unless it's too long
                // some wine APIs don't work 100% properly with it (like SDL_OpenURL with a file:/// URI)

                if(result.length() > MAX_PATH && !result.startsWith(US_("\\"))) {
                    result = UString{extPrefix} + result;
                }

                HeapFree(GetProcessHeap(), 0, dosPath);
                return result;
            }
        } else {
            tried_load_wine_func = true;
        }
        // Fallback: return as-is and let Wine handle it at file I/O layer
        return UString{filepath};
    }

    // Detect existing prefix type and determine output prefix
    std::string_view outputPrefix{""};
    size_t stripLen = 0;
    bool resolveAbsolute = false;

    if(filepath.starts_with(extUncPrefix)) {
        outputPrefix = extUncPrefix;
        stripLen = extUncPrefix.size();
    } else if(filepath.starts_with(extPrefix)) {
        outputPrefix = extPrefix;
        stripLen = extPrefix.size();
    } else if(filepath.starts_with(devicePrefix)) {
        outputPrefix = devicePrefix;
        stripLen = devicePrefix.size();
    } else if(filepath.starts_with(uncStart) && filepath.size() > 2 && filepath[2] != '?' && filepath[2] != '.') {
        // Standard UNC path -> extended UNC
        outputPrefix = extUncPrefix;
        stripLen = uncStart.size();
    } else {
        // Regular DOS path needs absolute resolution
        resolveAbsolute = true;

        // Don't prepend anything if we're running under Wine, see the reasoning above (broken interactions with some APIs)
        // Should work fine for not-long-paths anyways.
        if(!(filepath.length() < MAX_PATH && (RuntimePlatform::current() & RuntimePlatform::WIN_WINE))) {
            outputPrefix = extPrefix;
        }
    }

    std::string path{filepath.substr(stripLen)};
    File::normalizeSlashes(path, '/', '\\');

    if(resolveAbsolute) {
        UString widePath{path};
        DWORD len = GetFullPathNameW(widePath.wchar_str(), 0, nullptr, nullptr);
        if(len == 0) {
            logIfCV(debug_file, "GetFullPathNameW({}, 0, NULL, NULL): {:d}", widePath, GetLastError());
            return UString{filepath};
        }

        std::wstring buf(len, L'\0');
        len = GetFullPathNameW(widePath.wchar_str(), len, buf.data(), nullptr);
        if(len == 0) {
            logIfCV(debug_file, "GetFullPathNameW({}, {}, {}, NULL): {:d}", widePath, len, UString{buf},
                    GetLastError());
            return UString{filepath};
        }
        buf.resize(len);

        // GetFullPathNameW may return an already-prefixed path if CWD has an extended prefix
        std::wstring_view resolved{buf};
        if(resolved.starts_with(LR"(\\?\)") || resolved.starts_with(LR"(\\.\)")) {
            return UString{buf.data(), static_cast<int>(len)};
        }

        // Standard UNC path from GetFullPathNameW -> extended UNC
        if(resolved.starts_with(LR"(\\)") && resolved.size() > 2) {
            return UString{extUncPrefix} + UString{buf.data() + 2, static_cast<int>(len - 2)};
        }

        return UString{outputPrefix} + UString{buf.data(), static_cast<int>(len)};
    }

    return UString{outputPrefix} + UString{path};
}

UString adjustPath(std::string_view filepath) noexcept {
    if(!std_filesystem_supports_long_paths) return filepath;

    Sync::call_once(long_path_check, []() {
        std::array<wchar_t, MAX_PATH + 1> buf{};
        int length = static_cast<int>(GetModuleFileNameW(nullptr, buf.data(), MAX_PATH));
        if(length == 0) {
            std_filesystem_supports_long_paths = false;
            return;
        }

        UString uPath{buf.data(), length};
        uPath = adjustPath_(uPath.utf8View());

        if(uPath.length() == 0) {
            std_filesystem_supports_long_paths = false;
            return;
        }

        fs::path tempfs{uPath.plat_str()};
        std::error_code ec;

        auto status = fs::status(tempfs, ec);
        if(ec || status.type() == fs::file_type::not_found) {
            std_filesystem_supports_long_paths = false;
        }
    });

    if(!std_filesystem_supports_long_paths) {
        debugLog("NOTE: current std::filesystem implementation does not support long paths, disabling.");
        return filepath;
    }

    return adjustPath_(filepath);
}

}  // namespace

#else
#define adjustPath(dummy__) dummy__
#endif

fs::path File::getFsPath(std::string_view utf8path) {
    if(utf8path.empty()) return fs::path{};
#ifdef MCENGINE_PLATFORM_WINDOWS
    const UString filePathUStr{adjustPath(utf8path)};
    return fs::path{filePathUStr.wchar_str()};
#else
    return fs::path{utf8path};
#endif
}

FILE *File::fopen_c(const char *__restrict utf8filename, const char *__restrict modes) {
#ifdef MCENGINE_PLATFORM_WINDOWS
    if(utf8filename == nullptr || utf8filename[0] == '\0') return nullptr;
    const UString wideFilename{adjustPath(utf8filename)};
    const UString wideModes{modes};
    return _wfopen(wideFilename.wchar_str(), wideModes.wchar_str());
#else
    return fopen(utf8filename, modes);
#endif
}

int File::stat_c(const char *__restrict utf8filename, struct stat64 *__restrict buffer) {
#ifdef MCENGINE_PLATFORM_WINDOWS
    if(utf8filename == nullptr || utf8filename[0] == '\0' || buffer == nullptr) {
        errno = EINVAL;
        return -1;
    }
    const UString wideFilename{adjustPath(utf8filename)};
    return _wstat64(wideFilename.wchar_str(), buffer);
#else
    return stat64(utf8filename, buffer);
#endif
}

bool File::copy(std::string_view fromPath, std::string_view toPath) {
    if(fromPath.empty() || toPath.empty() || fromPath == toPath) {
        return false;
    }

    auto srcPath = getFsPath(fromPath);
    auto dstPath = getFsPath(toPath);

    std::error_code ec;
    fs::copy_file(srcPath, dstPath, fs::copy_options::overwrite_existing, ec);

    if(ec) {
        debugLog("File::copy failed ({:s} -> {:s}): {:s}", fromPath, toPath, ec.message());
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
// File implementation
//------------------------------------------------------------------------------
File::File(std::string_view filePath, MODE mode)
    : sFilePath(filePath), fsPath(getFsPath(filePath)), fileMode(mode), bReady(false) {
    logIfCV(debug_file, "Opening {:s}", this->sFilePath);

    if(mode == MODE::READ) {
        if(!openForReading()) return;
    } else if(mode == MODE::WRITE) {
        if(!openForWriting()) return;
    }

    this->bReady = true;
}

bool File::openForReading() {
    {
        // lazy redundant conversion
        std::string tempStdStr{this->sFilePath.utf8View()};
        const std::string origPath{this->sFilePath.utf8View()};

        // resolve the file path (handles case-insensitive matching)
        FILETYPE fileType = File::existsCaseInsensitive(tempStdStr, this->fsPath);

        if constexpr(Env::cfg(OS::WINDOWS)) {
            this->sFilePath = this->fsPath.wstring().c_str();
        } else {
            this->sFilePath = this->fsPath.string().c_str();
        }

        if(fileType != FILETYPE::FILE) {
            if(cv::debug_file.getBool()) {
                // usually the caller handles logging this sort of basic error
                debugLog("File Error: Path {:s} (orig: {:s}) {:s}", this->sFilePath, origPath,
                         fileType == FILETYPE::NONE ? "doesn't exist" : "is not a file");
            }
            return false;
        }
    }

    // create and open input file stream
    this->ifstream = std::make_unique<std::ifstream>();
    this->ifstream->open(this->fsPath, std::ios::in | std::ios::binary);

    // check if file opened successfully
    if(!this->ifstream || !this->ifstream->good()) {
        debugLog("File Error: Couldn't open file {:s}", this->sFilePath);
        return false;
    }

    // get file stats
    int ret = -1;
    // not using the stat_c helper because that would do redundant path conversion/validation
#ifdef MCENGINE_PLATFORM_WINDOWS
    ret = _wstat64(this->sFilePath.wchar_str(), &this->fsstat);
#else
    ret = stat64(this->sFilePath.toUtf8(), &this->fsstat);
#endif

    if(ret != 0) {
        debugLog("File Error: Couldn't get file size for {:s}: {}", this->sFilePath,
                 std::generic_category().message(errno));
        return false;
    }

    // validate file size
    if(this->getFileSize() == 0) {  // empty file is valid
        return true;
    } else if(std::cmp_greater(this->getFileSize(), 1024 * 1024 * cv::file_size_max.getInt())) {  // size sanity check
        debugLog("File Error: FileSize of {:s} is > {} MB!!!", this->sFilePath, cv::file_size_max.getInt());
        return false;
    }

    return true;
}

bool File::openForWriting() {
    // create parent directories if needed
    if(!this->fsPath.parent_path().empty()) {
        std::error_code ec;
        fs::create_directories(this->fsPath.parent_path(), ec);
        if(ec) {
            debugLog("File Error: Couldn't create parent directories for {:s} (error: {:s})", this->sFilePath,
                     ec.message());
            // continue anyway, the file open might still succeed if the directory exists
        }
    }

    // create and open output file stream
    this->ofstream = std::make_unique<std::ofstream>();
    this->ofstream->open(this->fsPath, std::ios::out | std::ios::trunc | std::ios::binary);

    // check if file opened successfully
    if(!this->ofstream->good()) {
        debugLog("File Error: Couldn't open file {:s} for writing", this->sFilePath);
        return false;
    }

    return true;
}

void File::write(const u8 *buffer, uSz size) {
    logIfCV(debug_file, "{:s} (canWrite: {})", this->sFilePath, canWrite());

    if(!canWrite()) return;

    this->ofstream->write(reinterpret_cast<const char *>(buffer), static_cast<std::streamsize>(size));
}

bool File::writeLine(std::string_view line, bool insertNewline) {
    if(!canWrite()) return false;

    // useless...
    if(insertNewline) {
        std::string lineNewline{std::string{line} + '\n'};
        this->ofstream->write(lineNewline.data(), static_cast<std::streamsize>(lineNewline.length()));
    } else {
        this->ofstream->write(line.data(), static_cast<std::streamsize>(line.length()));
    }
    return !this->ofstream->bad();
}

std::string File::readLine() {
    if(!canRead()) return "";

    std::string line;
    if(std::getline(*this->ifstream, line)) {
        // handle CRLF line endings
        if(!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        return line;
    }

    return "";
}

std::string File::readToString() {
    const auto size = getFileSize();
    if(size < 1) return "";

    const auto &bytes = readFile();
    if(bytes) return std::string{reinterpret_cast<const char *>(bytes.get()), size};

    return "";
}

uSz File::readBytes(uSz start, uSz amount, std::unique_ptr<u8[]> &out) {
    if(!canRead()) return 0;
    assert(out.get() != this->vFullBuffer.get() && "can't overwrite own buffer");

    if(start > this->getFileSize()) {
        logIfCV(debug_file, "tried to read {} starting from {}, but total size is only {}", this->sFilePath, start,
                this->getFileSize());
        return 0;
    }

    uSz end = (start + amount);
    if(end > this->getFileSize()) {
        end = this->getFileSize();
    }

    const uSz toRead = end - start;

    // return cached data if we have it
    if(!!this->vFullBuffer) {
        std::memcpy(out.get(), this->vFullBuffer.get() + start, toRead);
        return toRead;
    }

    this->ifstream->seekg(static_cast<std::streamsize>(start), std::ios::beg);
    this->ifstream->read(reinterpret_cast<char *>(out.get()), static_cast<std::streamsize>(toRead));

    return this->ifstream->gcount();
}

const std::unique_ptr<u8[]> &File::readFile() {
    logIfCV(debug_file, "{:s} (canRead: {})", this->sFilePath, this->bReady && canRead());

    // return cached buffer if already read
    if(!!this->vFullBuffer) return this->vFullBuffer;

    if(!this->bReady || !canRead()) {
        this->vFullBuffer.reset();
        return this->vFullBuffer;
    }

    // allocate buffer for file contents
    this->vFullBuffer = std::make_unique_for_overwrite<u8[]>(this->getFileSize());

    // read entire file
    this->ifstream->seekg(0, std::ios::beg);
    if(this->ifstream
           ->read(reinterpret_cast<char *>(this->vFullBuffer.get()), static_cast<std::streamsize>(this->getFileSize()))
           .good()) {
        return this->vFullBuffer;
    }

    this->vFullBuffer.reset();
    return this->vFullBuffer;
}

std::unique_ptr<u8[]> &&File::takeFileBuffer() {
    logIfCV(debug_file, "{:s} (canRead: {})", this->sFilePath, this->bReady && canRead());

    // if buffer is already populated, move it out
    if(!!this->vFullBuffer) return std::move(this->vFullBuffer);

    if(!this->bReady || !this->canRead()) {
        this->vFullBuffer.reset();
        return std::move(this->vFullBuffer);
    }

    // allocate buffer for file contents
    this->vFullBuffer = std::make_unique_for_overwrite<u8[]>(this->getFileSize());

    // read entire file
    this->ifstream->seekg(0, std::ios::beg);
    if(this->ifstream
           ->read(reinterpret_cast<char *>(this->vFullBuffer.get()), static_cast<std::streamsize>(this->getFileSize()))
           .good()) {
        return std::move(this->vFullBuffer);
    }

    // read failed, clear buffer and return empty vector
    this->vFullBuffer.reset();
    return std::move(this->vFullBuffer);
}

void File::readToVector(std::vector<u8> &out) {
    logIfCV(debug_file, "{:s} (canRead: {})", this->sFilePath, this->bReady && canRead());

    // if buffer is already populated, copy that
    if(!!this->vFullBuffer) {
        out.resize(this->getFileSize());
        std::memcpy(out.data(), this->vFullBuffer.get(), this->getFileSize() * sizeof(u8));
        return;
    }

    if(!this->bReady || !this->canRead()) {
        out.clear();
        return;
    }

    // allocate buffer for file contents
    out.resize(this->getFileSize());

    // read entire file
    this->ifstream->seekg(0, std::ios::beg);
    if(this->ifstream->read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(this->getFileSize()))
           .good()) {
        return;
    }

    // read failed, clear buffer and return empty vector
    out.clear();
    return;
}

#ifdef MCENGINE_PLATFORM_WASM
#include <emscripten/emscripten.h>

void File::flushToDisk() {
    // keep the runtime alive until the async sync completes, otherwise the IDB
    // connection gets torn down before the data reaches IndexedDB.
    // clang-format off
    EM_ASM(
        if(typeof FS !== 'undefined' && FS.syncfs) {
            runtimeKeepalivePush();
            FS.syncfs(false, function(e) {
                if(e) console.error('syncfs error:', e);
                runtimeKeepalivePop();
            });
        }
    );
    // clang-format on
}

#else

void File::flushToDisk() {}

#endif
