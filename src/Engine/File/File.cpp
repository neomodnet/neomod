//========== Copyright (c) 2012, PG & 2025, WH, All rights reserved. ============//
//
// purpose:		file wrapper, for cross-platform unicode path support
//
// $NoKeywords: $file
//===============================================================================//

#include "File.h"
#include "ConVar.h"
#include "Engine.h"
#include "Hashing.h"
#include "Logging.h"
#include "SyncMutex.h"
#include "SyncOnce.h"
#include "UniString.h"

#define WANT_PDQSORT
#include "Sorting.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
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

    // what a lookup resolves to: the entry's actual name on disk, and its type
    struct EntryPair {
        std::string name;
        File::FILETYPE type;
    };

    struct DirectoryEntry {
        Hash::unstable_ncase_stringmap<File::FILETYPE> files;  // keyed by the actual filename
        chrono::steady_clock::time_point lastCacheAccess;
        fs::file_time_type lastModified;
    };

    // look up a file with case-insensitive matching
    EntryPair lookup(const fs::path &dirPath, std::string_view filename) {
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
            std::vector<File::DirEntry> listing;
            File::getDirectoryEntries(dirKey, File::DirContents::ALL, listing, /*withMetadata=*/false);
            for(auto &dirEntry : listing) {
                newEntry.files[std::move(dirEntry.name)] = dirEntry.type;
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
        if(fileIt != entry->files.end()) return {fileIt->first, fileIt->second};

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
// each platform provides enumerate_directory(path, types, withMetadata, sink): the listing filtered by types, handed
// to sink(File::DirEntry) one entry at a time (. and .. skipped). withMetadata fills in mtime/size, which costs a
// stat per entry where the listing itself doesn't carry them

namespace {
// needed for MSVC
#if !defined(S_ISREG) && defined(S_IFMT) && defined(S_IFREG)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#if !defined(S_ISDIR) && defined(S_IFMT) && defined(S_IFDIR)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

File::FILETYPE type_from_stat(const struct stat64 &st) {
    using enum File::FILETYPE;
    if(S_ISDIR(st.st_mode)) return FOLDER;
    if(S_ISREG(st.st_mode)) return FILE;

#ifdef S_ISLNK
    if(S_ISLNK(st.st_mode)) {
        logIfCV(debug_file, "WARNING: file is symlink (unfollowed, unknown type)");
    }
#endif

    return OTHER;
}

}  // namespace

#ifndef MCENGINE_PLATFORM_WINDOWS
namespace {
u32 stat_to_nsec_mtime(const struct stat64 &st) noexcept {
#ifdef MCENGINE_PLATFORM_MACOS
    return static_cast<u32>(st.st_mtimespec.tv_nsec);
#else
    return static_cast<u32>(st.st_mtim.tv_nsec);
#endif
}
}  // namespace
#endif

#if ((defined(MCENGINE_PLATFORM_MACOS) || defined(MCENGINE_PLATFORM_LINUX)) && defined(_GNU_SOURCE)) || \
    ((defined(_POSIX_C_SOURCE) && (_POSIX_C_SOURCE >= 200809L)) || defined(_ATFILE_SOURCE))

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef MCENGINE_PLATFORM_MACOS
#include <sys/attr.h>
#include <sys/vnode.h>
#endif

namespace {
#ifdef MCENGINE_PLATFORM_MACOS
// the listing with names, types, mtimes and sizes already attached, hundreds of entries per syscall instead of an
// fstatat per entry. false if the filesystem doesn't support it (the first call fails, nothing consumed)
template <typename Sink>
bool enumerate_directory_bulk(int fd, bool wantDirectories, bool wantFiles, const Sink &sink) noexcept {
    attrlist attrs{};
    attrs.bitmapcount = ATTR_BIT_MAP_COUNT;
    attrs.commonattr = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME | ATTR_CMN_OBJTYPE | ATTR_CMN_MODTIME;
    attrs.fileattr = ATTR_FILE_DATALENGTH;

    alignas(8) char buffer[64 * 1024];
    for(bool first = true;; first = false) {
        const int count = getattrlistbulk(fd, &attrs, &buffer[0], sizeof(buffer), 0);
        if(count < 0) {
            if(first) return false;
            debugLog("getattrlistbulk failed: {}", strerror(errno));
            return true;  // what was listed so far stands, like readdir stopping early
        }
        if(count == 0) return true;

        // packed records: u32 length, an attribute_set_t of what's present, then those attributes in bit order (the
        // name as an attrreference_t to a nul-terminated string). 4-byte aligned, so everything is read by memcpy
        const char *record = &buffer[0];
        for(int i = 0; i < count; i++) {
            const char *p = record;
            u32 length;  // NOLINT
            std::memcpy(&length, p, sizeof(length));
            p += sizeof(length);
            record += length;

            attribute_set_t returned;
            std::memcpy(&returned, p, sizeof(returned));
            p += sizeof(returned);
            if(!(returned.commonattr & ATTR_CMN_NAME)) continue;

            attrreference_t nameRef;
            std::memcpy(&nameRef, p, sizeof(nameRef));
            const char *name = p + nameRef.attr_dataoffset;  // (relative to the reference itself)
            const uSz nameLength = nameRef.attr_length > 0 ? nameRef.attr_length - 1 : 0;  // (counts the nul)
            p += sizeof(nameRef);

            fsobj_type_t objType = VNON;
            if(returned.commonattr & ATTR_CMN_OBJTYPE) {
                std::memcpy(&objType, p, sizeof(objType));
                p += sizeof(objType);
            }
            timespec modTime{};
            if(returned.commonattr & ATTR_CMN_MODTIME) {
                std::memcpy(&modTime, p, sizeof(modTime));
                p += sizeof(modTime);
            }
            off_t size = 0;  // (only files have one)
            if(returned.fileattr & ATTR_FILE_DATALENGTH) {
                std::memcpy(&size, p, sizeof(size));
            }

            File::FILETYPE type = objType == VDIR   ? File::FILETYPE::FOLDER
                                  : objType == VREG ? File::FILETYPE::FILE
                                                    : File::FILETYPE::OTHER;
            if(objType == VLNK || !(returned.commonattr & ATTR_CMN_OBJTYPE) ||
               !(returned.commonattr & ATTR_CMN_MODTIME)) {
                // what was listed is the link itself, and symlinks count as what they point at
                struct stat64 st;                               // NOLINT
                if(fstatat64(fd, name, &st, 0) != 0) continue;  // dangling
                type = type_from_stat(st);
                modTime = st.st_mtimespec;
                size = st.st_size;
            }

            const bool isDir = type == File::FILETYPE::FOLDER;
            if(!((wantDirectories && isDir) || (wantFiles && !isDir))) continue;

            sink(File::DirEntry{.name = std::string{name, nameLength},
                                .type = type,
                                .mtime = static_cast<i64>(modTime.tv_sec),
                                .mtime_nsec = static_cast<u32>(modTime.tv_nsec),
                                .size = isDir ? 0 : static_cast<u64>(size)});
        }
    }
}
#endif

template <typename Sink>
bool enumerate_directory(std::string_view pathToEnumView, File::DirContents types, bool withMetadata,
                         const Sink &sink) noexcept {
    using namespace flags::operators;
    const bool wantDirectories = !!(types & File::DirContents::DIRECTORIES);
    const bool wantFiles = !!(types & File::DirContents::FILES);

    const std::string pathToEnum{pathToEnumView};
    const int fd = openat64(AT_FDCWD, pathToEnum.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    if(fd == -1) {
        debugLog("openat64 failed on {}: {}", pathToEnum, strerror(errno));
        return false;
    }

#ifdef MCENGINE_PLATFORM_MACOS
    if(withMetadata && enumerate_directory_bulk(fd, wantDirectories, wantFiles, sink)) {
        close(fd);
        return true;
    }
#endif

    DIR *dir = fdopendir(fd);
    if(!dir) {
        debugLog("fdopendir failed on {}: {}", pathToEnum, strerror(errno));
        close(fd);
        return false;
    }

    struct dirent64 *entry;  // NOLINT
    while((entry = readdir64(dir)) != nullptr) {
        const char *name = &entry->d_name[0];

        // skip . and ..
        if(name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }

        // d_type is supported on most linux filesystems (ext4, xfs, btrfs, etc)
        // but may be DT_UNKNOWN on network filesystems
        struct stat64 st;  // NOLINT
        bool stated = false;
        File::FILETYPE type;  // NOLINT
        if(entry->d_type != DT_UNKNOWN && entry->d_type != DT_LNK) {
            type = entry->d_type == DT_DIR   ? File::FILETYPE::FOLDER
                   : entry->d_type == DT_REG ? File::FILETYPE::FILE
                                             : File::FILETYPE::OTHER;
        } else {
            // fallback for filesystems that don't populate d_type (and for symlinks, which count as what they point
            // at). relative to the open directory, so there's no path walk per entry
            if(fstatat64(fd, name, &st, 0) != 0) continue;  // gone in the meantime, or a dangling symlink
            stated = true;
            type = type_from_stat(st);
        }

        const bool isDir = type == File::FILETYPE::FOLDER;
        if(!((wantDirectories && isDir) || (wantFiles && !isDir))) continue;

        File::DirEntry out{.name = name, .type = type};
        if(withMetadata) {
            if(!stated && fstatat64(fd, name, &st, 0) != 0) continue;
            out.mtime = static_cast<i64>(st.st_mtime);
            out.mtime_nsec = stat_to_nsec_mtime(st);
            out.size = isDir ? 0 : static_cast<u64>(st.st_size);
        }
        sink(std::move(out));
    }

    closedir(dir);  // also closes fd

    return true;
}
}  // namespace

#elif defined(MCENGINE_PLATFORM_WINDOWS)  // the win32 api is just WAY faster for this than std::filesystem

#include "dynutils.h"
#include "RuntimePlatform.h"

#include "WinDebloatDefs.h"
#include <fileapi.h>
#include <libloaderapi.h>
#include <heapapi.h>
#include <errhandlingapi.h>
#include <handleapi.h>
#include <winerror.h>

#include <string>
namespace {

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR) - 1)
#endif

Sync::once_flag long_path_check;
bool std_filesystem_supports_long_paths{true};

using wine_get_dos_file_name_t = LPWSTR CDECL(LPCSTR);
wine_get_dos_file_name_t *pwine_get_dos_file_name{nullptr};
bool tried_load_wine_func{false};

void normalize_wide_slashes(std::wstring &str, wchar_t oldSlash, wchar_t newSlash) noexcept {
    std::ranges::replace(str, oldSlash, newSlash);

    bool prev = false;
    std::erase_if(str, [&](wint_t c) {
        const bool remove = prev && c == newSlash;
        prev = (c == newSlash);
        return remove;
    });
}

std::wstring adjust_path_(std::string_view filepathNarrow, bool forceLongPath) noexcept {
    static constexpr const std::wstring_view extPrefix{LR"(\\?\)"sv};
    static constexpr const std::wstring_view devicePrefix{LR"(\\.\)"sv};
    static constexpr const std::wstring_view extUncPrefix{LR"(\\?\UNC\)"sv};
    static constexpr const std::wstring_view uncStart{LR"(\\)"sv};

    if(filepathNarrow.empty()) return {};

    std::wstring filepath;

    // Handle Unix absolute paths under Wine
    if(filepathNarrow[0] == '/' && (filepathNarrow.size() < 2 || filepathNarrow[1] != '/')) {
        if(pwine_get_dos_file_name ||
           (!tried_load_wine_func && (RuntimePlatform::current() & RuntimePlatform::WIN_WINE) &&
            (pwine_get_dos_file_name = dynutils::load_func<wine_get_dos_file_name_t>(
                 reinterpret_cast<dynutils::lib_obj *>(GetModuleHandleA("kernel32.dll")), "wine_get_dos_file_name")))) {
            std::string path{filepathNarrow};
            File::normalizeSlashes(path, '\\', '/');  // normalize any mixed slashes in the path portion

            LPWSTR dosPath = pwine_get_dos_file_name(path.c_str());  // use original with forward slashes
            if(dosPath) {
                filepath = std::wstring{dosPath, std::wcslen(dosPath)};
                HeapFree(GetProcessHeap(), 0, dosPath);

                if(!forceLongPath) {
                    // not sure if this is necessary or works as intended...
                    // but don't return an extended path unless it's too long
                    // some wine APIs don't work 100% properly with it (like SDL_OpenURL with a file:/// URI)
                    if(filepath.length() > MAX_PATH && !filepath.starts_with(L'\\')) {
                        return std::wstring{extPrefix} + filepath;
                    }
                    return filepath;
                }
                // forceLongPath: use resolved DOS path as input to the main path logic below
            }
        } else {
            tried_load_wine_func = true;
        }
        if(filepath.empty()) {
            // Fallback: return as-is and let Wine handle it at file I/O layer
            return UniString::to_wide(filepathNarrow);
        }
    }

    // Detect existing prefix type and determine output prefix
    if(filepath.empty()) filepath = UniString::to_wide(filepathNarrow);
    std::wstring outputPrefix;
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
    } else if(filepath.starts_with(uncStart) && filepath.size() > 2 && filepath[2] != L'?' && filepath[2] != L'.') {
        // Standard UNC path -> extended UNC
        outputPrefix = extUncPrefix;
        stripLen = uncStart.size();
    } else {
        // Regular DOS path needs absolute resolution
        resolveAbsolute = true;

        // Don't prepend anything if we're running under Wine, see the reasoning above (broken interactions with some APIs)
        // Should work fine for not-long-paths anyways. forceLongPath overrides this for APIs that need the prefix.
        if(forceLongPath ||
           !(filepath.length() < MAX_PATH && (RuntimePlatform::current() & RuntimePlatform::WIN_WINE))) {
            outputPrefix = extPrefix;
        }
    }

    std::wstring path{filepath.substr(stripLen)};
    normalize_wide_slashes(path, L'/', L'\\');

    if(resolveAbsolute) {
        DWORD len = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
        if(len == 0) {
            logIfCV(debug_file, "GetFullPathNameW({}, 0, NULL, NULL): {:d}", UniString::to_utf8(path), GetLastError());
            return filepath;
        }

        std::wstring buf(len, L'\0');
        len = GetFullPathNameW(path.c_str(), len, buf.data(), nullptr);
        if(len == 0) {
            logIfCV(debug_file, "GetFullPathNameW({}, {}, {}, NULL): {:d}", UniString::to_utf8(path), len,
                    UniString::to_utf8(buf), GetLastError());
            return filepath;
        }
        buf.resize(len);

        // GetFullPathNameW may return an already-prefixed path if CWD has an extended prefix
        std::wstring_view resolved{buf};
        if(resolved.starts_with(LR"(\\?\)") || resolved.starts_with(LR"(\\.\)")) {
            return std::wstring{buf.data(), len};
        }

        // Standard UNC path from GetFullPathNameW -> extended UNC
        if(resolved.starts_with(LR"(\\)") && resolved.size() > 2) {
            return std::wstring{extUncPrefix} + std::wstring{buf.data() + 2, len - 2};
        }

        return outputPrefix + std::wstring{buf.data(), static_cast<size_t>(len)};
    }

    return outputPrefix + path;
}

std::wstring adjust_path(std::string_view filepath) noexcept {
    if(!std_filesystem_supports_long_paths) return UniString::to_wide(filepath);

    Sync::call_once(long_path_check, []() {
        std::array<wchar_t, MAX_PATH + 1> buf{};
        const size_t length = static_cast<size_t>(GetModuleFileNameW(nullptr, buf.data(), MAX_PATH));
        if(length == 0) {
            std_filesystem_supports_long_paths = false;
            return;
        }

        std::string narrowPath{UniString::to_utf8(std::wstring_view{buf.data(), length})};
        std::wstring wPath = adjust_path_(narrowPath, false);

        if(wPath.length() == 0) {
            std_filesystem_supports_long_paths = false;
            return;
        }

        fs::path tempfs{wPath.c_str()};
        std::error_code ec;

        auto status = fs::status(tempfs, ec);
        if(ec || status.type() == fs::file_type::not_found) {
            std_filesystem_supports_long_paths = false;
        }
    });

    if(!std_filesystem_supports_long_paths) {
        debugLog("NOTE: current std::filesystem implementation does not support long paths, disabling.");
        return UniString::to_wide(filepath);
    }

    return adjust_path_(filepath, false);
}

#ifndef FILE_FLAG_BACKUP_SEMANTICS
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000
#endif

#ifndef FILE_LIST_DIRECTORY
#define FILE_LIST_DIRECTORY 0x0001
#endif

// a filetime (100ns intervals since 1601) as unix seconds + the sub-second remainder, straight from utc
struct UnixTime {
    i64 sec;
    u32 nsec;
};

UnixTime file_time_to_unix(i64 fileTime) noexcept {
    const i64 t = fileTime - 116444736000000000LL;
    i64 sec = t / 10000000LL;
    i64 rem = t % 10000000LL;
    if(rem < 0) {  // (a pre-1970 timestamp)
        sec--;
        rem += 10000000LL;
    }
    return {.sec = sec, .nsec = static_cast<u32>(rem * 100)};
}

i64 file_time_to_int(const FILETIME &ft) noexcept {
    return static_cast<i64>((static_cast<u64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime);
}

// the attributes of what a path points at (symlinks/junctions are followed, like stat() does)
bool get_attributes_w(const wchar_t *path, WIN32_FILE_ATTRIBUTE_DATA &data) noexcept {
    if(!GetFileAttributesExW(path, GetFileExInfoStandard, &data)) return false;
    if(!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return true;

    HANDLE handle = CreateFileW(path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if(handle == INVALID_HANDLE_VALUE /* NOLINT */) return false;
    BY_HANDLE_FILE_INFORMATION info;
    const bool ok = GetFileInformationByHandle(handle, &info);
    CloseHandle(handle);
    if(!ok) return false;

    data.dwFileAttributes = info.dwFileAttributes;
    data.ftCreationTime = info.ftCreationTime;
    data.ftLastAccessTime = info.ftLastAccessTime;
    data.ftLastWriteTime = info.ftLastWriteTime;
    data.nFileSizeHigh = info.nFileSizeHigh;
    data.nFileSizeLow = info.nFileSizeLow;
    return true;
}

// stat64 from the win32 attributes
int stat_w(const wchar_t *path, struct stat64 *st) noexcept {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if(!get_attributes_w(path, data)) {
        const DWORD err = GetLastError();
        errno =
            (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND || err == ERROR_INVALID_NAME) ? ENOENT : EACCES;
        return -1;
    }
    *st = {};
    st->st_mode = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? _S_IFDIR : _S_IFREG;
    st->st_size = static_cast<decltype(st->st_size)>((static_cast<u64>(data.nFileSizeHigh) << 32) | data.nFileSizeLow);
    st->st_mtime = file_time_to_unix(file_time_to_int(data.ftLastWriteTime)).sec;
    st->st_atime = file_time_to_unix(file_time_to_int(data.ftLastAccessTime)).sec;
    st->st_ctime = file_time_to_unix(file_time_to_int(data.ftCreationTime)).sec;  // (what the crt puts there)
    return 0;
}

// the metadata a listing carries for an entry. a listing only describes a link itself, so the target's is looked up
// for those
void set_listed_metadata(File::DirEntry &out, DWORD attrs, i64 writeTime, u64 size, std::wstring_view dirPath,
                         std::wstring_view name) noexcept {
    if(attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
        std::wstring target{dirPath};
        if(!target.ends_with(L'\\') && !target.ends_with(L'/')) target.push_back(L'\\');
        target.append(name);
        if(WIN32_FILE_ATTRIBUTE_DATA data; get_attributes_w(target.c_str(), data)) {
            attrs = data.dwFileAttributes;
            writeTime = file_time_to_int(data.ftLastWriteTime);
            size = (static_cast<u64>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        }
    }
    const auto [sec, nsec] = file_time_to_unix(writeTime);
    out.mtime = sec;
    out.mtime_nsec = nsec;
    out.size = (attrs & FILE_ATTRIBUTE_DIRECTORY) ? 0 : size;
}

// reference:
// https://github.com/xfeeefeee/node-windows-readdir-fast/blob/main/readdirFast.cc

using NTSTATUS = int32_t;

// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_io_status_block?redirectedfrom=MSDN
typedef struct _IO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        void *Pointer;
    };
    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

// https://learn.microsoft.com/en-us/windows/win32/api/ntdef/ns-ntdef-_unicode_string
typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    wchar_t *Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

// See the handy table linked on the page below to learn where these values comes from.
// https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/using-ntstatus-values
inline constexpr NTSTATUS STATUS_NO_MORE_FILES{(NTSTATUS)0x80000006};
inline constexpr NTSTATUS STATUS_NO_SUCH_FILE{(NTSTATUS)0xC000000F};
[[maybe_unused]] inline constexpr NTSTATUS STATUS_NOT_IMPLEMENTED{(NTSTATUS)0xC0000002};

inline constexpr DWORD FileDirectoryInformation{0x1};

typedef struct _FILE_DIRECTORY_INFORMATION {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    wchar_t FileName[1];
} FILE_DIRECTORY_INFORMATION;

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntquerydirectoryfile
using NtQueryDirectoryFile_t = NTSTATUS NTAPI(
    HANDLE FileHandle, HANDLE Event,
    // This here is PIO_APC_ROUTINE, but we don't use APCs and just set it to null.
    void *ApcRoutine, void *ApcContext, IO_STATUS_BLOCK *IoStatusBlock, void *FileInformation, ULONG Length,
    // This is FILE_INFORMATION_CLASS, which I am not going to paste here.
    // https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ne-wdm-_file_information_class
    DWORD FileInformationClass, BOOLEAN ReturnSingleEntry, UNICODE_STRING *FileName, BOOLEAN RestartScan);

NtQueryDirectoryFile_t *pNtQueryDirectoryFile{nullptr};

Sync::once_flag ntqdf_loaded_once;
bool ntqdf_available{false};

bool try_load_NTQDF() noexcept {
    Sync::call_once(ntqdf_loaded_once, []() {
        auto *ntdll_handle{reinterpret_cast<dynutils::lib_obj *>(GetModuleHandle(TEXT("ntdll.dll")))};
        if(ntdll_handle) {
            pNtQueryDirectoryFile = dynutils::load_func<NtQueryDirectoryFile_t>(ntdll_handle, "NtQueryDirectoryFile");
        }
        if(!pNtQueryDirectoryFile) return;

        // sanity check to see if the function actually works (e.g. not STATUS_NOT_IMPLEMENTED under Wine)
        HANDLE testDir = CreateFileW(                                //
            L".",                                                    //
            FILE_LIST_DIRECTORY,                                     //
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,  //
            nullptr,                                                 //
            OPEN_EXISTING,                                           //
            FILE_FLAG_BACKUP_SEMANTICS,                              //
            nullptr                                                  //
        );
        if(testDir == INVALID_HANDLE_VALUE /* NOLINT */) return;

        alignas(8) uint8_t buf[1 * 1024];
        IO_STATUS_BLOCK iosb{};
        NTSTATUS st = pNtQueryDirectoryFile(  //
            testDir,                          //
            nullptr,                          //
            nullptr,                          //
            nullptr,                          //
            &iosb,                            //
            &buf[0],                          //
            sizeof(buf),                      //
            FileDirectoryInformation,         //
            FALSE,                            // ReturnSingleEntry
            nullptr,                          //
            TRUE                              // RestartScan
        );
        CloseHandle(testDir);
        ntqdf_available = NT_SUCCESS(st);
    });
    return ntqdf_available;
}

template <typename Sink>
bool readdir_NTAPI(const std::wstring &dirPath, File::DirContents types, bool withMetadata, const Sink &sink) noexcept {
    using enum File::DirContents;
    using namespace flags::operators;
    const bool wantDirectories = !!(types & DIRECTORIES);
    const bool wantFiles = !!(types & FILES);

    HANDLE dirHandle = CreateFileW(                              //
        dirPath.c_str(),                                         //
        FILE_LIST_DIRECTORY,                                     //
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,  //
        nullptr,                                                 //
        OPEN_EXISTING,                                           //
        FILE_FLAG_BACKUP_SEMANTICS,                              //
        nullptr                                                  //
    );
    if(dirHandle == INVALID_HANDLE_VALUE /* NOLINT */) return false;

    alignas(8) uint8_t buffer[64 * 1024];  // output buffer
    IO_STATUS_BLOCK statusBlock{};

    NTSTATUS status = pNtQueryDirectoryFile(  //
        dirHandle,                            //
        nullptr,                              //
        nullptr,                              //
        nullptr,                              //
        &statusBlock,                         //
        &buffer[0],                           //
        sizeof(buffer),                       //
        FileDirectoryInformation,             //
        FALSE,                                // ReturnSingleEntry
        nullptr,                              //
        TRUE                                  // RestartScan
    );

    if(status == STATUS_NO_SUCH_FILE || status == STATUS_NO_MORE_FILES) {
        CloseHandle(dirHandle);
        return true;  // empty directory
    }
    if(!NT_SUCCESS(status)) {
        CloseHandle(dirHandle);
        return false;
    }

    for(;;) {
        auto *entry = reinterpret_cast<FILE_DIRECTORY_INFORMATION *>(buffer);
        for(;;) {
            const wchar_t *name = &entry->FileName[0];
            const size_t cchName = entry->FileNameLength / sizeof(wchar_t);

            // skip . and ..
            const bool isDot =
                (cchName == 1 && name[0] == L'.') || (cchName == 2 && name[0] == L'.' && name[1] == L'.');
            if(!isDot) {
                const bool isDir = !!(entry->FileAttributes & FILE_ATTRIBUTE_DIRECTORY);
                if((wantDirectories && isDir) || (wantFiles && !isDir)) {
                    File::DirEntry out{.name = UniString::to_utf8(std::wstring_view{name, cchName}),
                                       .type = isDir ? File::FILETYPE::FOLDER : File::FILETYPE::FILE};
                    if(withMetadata) {
                        set_listed_metadata(out, entry->FileAttributes, entry->LastWriteTime.QuadPart,
                                            static_cast<u64>(entry->EndOfFile.QuadPart), dirPath, {name, cchName});
                    }
                    sink(std::move(out));
                }
            }

            if(entry->NextEntryOffset == 0) break;
            entry = reinterpret_cast<FILE_DIRECTORY_INFORMATION *>(reinterpret_cast<uint8_t *>(entry) +
                                                                   entry->NextEntryOffset);
        }

        // fetch next batch (state is tied to the directory handle)
        status = pNtQueryDirectoryFile(  //
            dirHandle,                   //
            nullptr,                     //
            nullptr,                     //
            nullptr,                     //
            &statusBlock,                //
            &buffer[0],                  //
            sizeof(buffer),              //
            FileDirectoryInformation,    //
            FALSE,                       // ReturnSingleEntry
            nullptr,                     //
            FALSE                        // RestartScan
        );
        if(status == STATUS_NO_MORE_FILES) break;
        if(!NT_SUCCESS(status)) break;
    }

    CloseHandle(dirHandle);

    return true;
}

template <typename Sink>
bool readdir_Win32(std::string_view pathToEnum, File::DirContents types, bool withMetadata, const Sink &sink) noexcept {
    using enum File::DirContents;
    using namespace flags::operators;
    const bool wantDirectories = !!(types & DIRECTORIES);
    const bool wantFiles = !!(types & FILES);

    std::wstring folder{UniString::to_wide(pathToEnum)};

    // Win32: needs path\*.* search pattern
    // TODO: avoid this fragile bs
    std::wstring endSep{L'/'};
    std::wstring otherSep{L'\\'};
    // for UNC/long paths, make sure we use a backslash as the last separator
    if(folder.starts_with(LR"(\\?\)") || folder.starts_with(LR"(\\.\)")) {
        endSep = L'\\';
        otherSep = L'/';
    }
    if(!folder.ends_with(endSep)) {
        if(folder.ends_with(otherSep)) {
            folder.pop_back();
        }
        folder.append(endSep);
    }
    const std::wstring pattern{folder + L"*.*"};

    WIN32_FIND_DATAW data{};
    HANDLE handle = FindFirstFileW(pattern.c_str(), &data);
    if(handle != INVALID_HANDLE_VALUE /* NOLINT */) {
        do {
            const wchar_t *wFilename = &data.cFileName[0];
            const size_t length = std::wcslen(wFilename);
            if(length == 0) continue;
            // skip . and ..
            if(wFilename[0] == L'.' && (wFilename[1] == L'\0' || (wFilename[1] == L'.' && wFilename[2] == L'\0'))) {
                continue;
            }

            const bool isDir = !!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
            if((wantDirectories && isDir) || (wantFiles && !isDir)) {
                File::DirEntry out{.name = UniString::to_utf8(std::wstring_view{wFilename, length}),
                                   .type = isDir ? File::FILETYPE::FOLDER : File::FILETYPE::FILE};
                if(withMetadata) {
                    set_listed_metadata(out, data.dwFileAttributes, file_time_to_int(data.ftLastWriteTime),
                                        (static_cast<u64>(data.nFileSizeHigh) << 32) | data.nFileSizeLow, folder,
                                        {wFilename, length});
                }
                sink(std::move(out));
            }
        } while(FindNextFileW(handle, &data));

        FindClose(handle);
    } else {
        return false;
    }

    return true;
}

// windows impl
template <typename Sink>
bool enumerate_directory(std::string_view pathToEnum, File::DirContents types, bool withMetadata,
                         const Sink &sink) noexcept {
    if(try_load_NTQDF()) {
        return readdir_NTAPI(adjust_path_(pathToEnum, true), types, withMetadata, sink);
    }

    return readdir_Win32(pathToEnum, types, withMetadata, sink);
}

}  // namespace

#else

namespace {
// for getting files in folder/ folders in folder (generic implementation)
template <typename Sink>
bool enumerate_directory(std::string_view pathToEnum, File::DirContents types, bool withMetadata,
                         const Sink &sink) noexcept {
    using namespace flags::operators;
    const bool wantDirectories = !!(types & File::DirContents::DIRECTORIES);
    const bool wantFiles = !!(types & File::DirContents::FILES);

    bool listedAny = false;
    std::error_code ec;
    for(auto it = fs::directory_iterator(pathToEnum, ec); it != fs::directory_iterator{}; it.increment(ec)) {
        const auto &entry = *it;
        std::error_code statusEc;
        const auto fileType = entry.status(statusEc).type();
        if(statusEc || fileType == fs::file_type::not_found) continue;  // gone in the meantime, or a dangling symlink

        const File::FILETYPE type = fileType == fs::file_type::directory ? File::FILETYPE::FOLDER
                                    : fileType == fs::file_type::regular ? File::FILETYPE::FILE
                                                                         : File::FILETYPE::OTHER;
        const bool isDir = type == File::FILETYPE::FOLDER;
        if(!((wantDirectories && isDir) || (wantFiles && !isDir))) continue;

        File::DirEntry out{.name = entry.path().filename().generic_string(), .type = type};
        if(withMetadata) {
            struct stat64 st{};
            if(File::stat_c(entry.path().string().c_str(), &st) != 0) continue;
            out.mtime = static_cast<i64>(st.st_mtime);
            out.mtime_nsec = stat_to_nsec_mtime(st);
            out.size = isDir ? 0 : static_cast<u64>(st.st_size);
        }
        sink(std::move(out));
        listedAny = true;
    }

    if(ec && !listedAny) {
        debugLog("Failed to enumerate directory: {}", ec.message());
        return false;
    }

    return true;
}
}  // namespace

#endif  // MCENGINE_PLATFORM_WINDOWS

bool File::getDirectoryEntries(std::string_view toEnumerate, DirContents types,
                               std::vector<std::string> &utf8NamesOut) noexcept {
    if(toEnumerate.empty()) return false;
    return enumerate_directory(toEnumerate, types, false,
                               [&utf8NamesOut](DirEntry entry) { utf8NamesOut.push_back(std::move(entry.name)); });
}

bool File::getDirectoryEntries(std::string_view toEnumerate, DirContents types, std::vector<DirEntry> &entriesOut,
                               bool withMetadata) noexcept {
    if(toEnumerate.empty()) return false;
    return enumerate_directory(toEnumerate, types, withMetadata,
                               [&entriesOut](DirEntry entry) { entriesOut.push_back(std::move(entry)); });
}

#ifndef MCENGINE_PLATFORM_WINDOWS
#define adjust_path(dummy__) dummy__
#endif

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
    using enum File::FILETYPE;
    if(filePath.empty()) return NONE;

#ifdef MCENGINE_PLATFORM_WINDOWS
    // this is faster than stat on windows
    if(DWORD attrs = GetFileAttributesW(path.wstring().c_str()); attrs != INVALID_FILE_ATTRIBUTES) {
        return (attrs & FILE_ATTRIBUTE_DIRECTORY) ? FOLDER : FILE /* assume file */;
    } else {
        return NONE;
    }
#else
    struct stat64 tempst{};
    const int statRet = stat64(path.string().c_str(), &tempst);

    if(statRet != 0) {
        // path not found, try case-insensitive lookup
        return MAYBE_INSENSITIVE;
    }

    return type_from_stat(tempst);
#endif
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

fs::path File::getFsPath(std::string_view utf8path) {
    if(utf8path.empty()) return fs::path{};
#ifdef MCENGINE_PLATFORM_WINDOWS
    return fs::path{adjust_path(utf8path)};
#else
    return fs::path{utf8path};
#endif
}

FILE *File::fopen_c(const char *__restrict utf8filename, const char *__restrict modes) noexcept {
#ifdef MCENGINE_PLATFORM_WINDOWS
    if(utf8filename == nullptr || utf8filename[0] == '\0') return nullptr;
    const std::wstring wideFilename{adjust_path(std::string_view{utf8filename})};
    const std::wstring wideModes{UniString::to_wide(std::string_view{modes})};
    return _wfopen(wideFilename.c_str(), wideModes.c_str());
#else
    return fopen(utf8filename, modes);
#endif
}

int File::stat_c(const char *__restrict utf8filename, struct stat64 *__restrict buffer) noexcept {
#ifdef MCENGINE_PLATFORM_WINDOWS
    if(utf8filename == nullptr || utf8filename[0] == '\0' || buffer == nullptr) {
        errno = EINVAL;
        return -1;
    }
    const std::wstring wideFilename{adjust_path(std::string_view{utf8filename})};
    return stat_w(wideFilename.c_str(), buffer);
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
    int statRet = -1;
    // get file stats
    // not using the stat_c helper because that would do redundant path conversion/validation
#ifdef MCENGINE_PLATFORM_WINDOWS
    statRet = stat_w(this->fsPath.c_str(), &this->fsstat);
    if(statRet == 0) {
        // convert fs::path back to utf8
        this->sFilePath = UniString::to_utf8(this->fsPath.wstring());
    }
#else
    statRet = stat64(this->sFilePath.c_str(), &this->fsstat);
    if(statRet != 0) {
        // try case-insensitive lookup if stat failed
        if(File::existsCaseInsensitive(this->sFilePath, this->fsPath) != FILETYPE::NONE) {
            // re-stat
            statRet = stat64(this->sFilePath.c_str(), &this->fsstat);
        }
    }
#endif

    if(statRet != 0) {
        // usually the caller handles logging this sort of basic error
        logIfCV(debug_file, "File Error: Couldn't stat() file {:s}: {}", this->sFilePath,
                std::generic_category().message(errno));
        return false;
    }

    // get type from stat
    if(type_from_stat(this->fsstat) != FILETYPE::FILE) {
        logIfCV(debug_file, "File Error: Path {:s} {:s}", this->sFilePath, "is not a file");
        return false;
    }

    // validate file size
    if(this->getFileSize() == 0) {  // empty file is valid
        return true;
    } else if(std::cmp_greater(this->getFileSize(), 1024 * 1024 * cv::file_size_max.getInt())) {  // size sanity check
        debugLog("File Error: FileSize of {:s} is > {} MB!!!", this->sFilePath, cv::file_size_max.getInt());
        return false;
    }

    // create and open input file stream
    this->ifstream = std::make_unique<std::ifstream>();
    assert(this->ifstream);
    this->ifstream->open(this->fsPath, std::ios::in | std::ios::binary);

    // check if file opened successfully
    if(!this->ifstream->good()) {
        debugLog("File Error: Couldn't open file {:s} (fsPath: {:s}) for reading", this->sFilePath,
                 Env::cfg(OS::WINDOWS) ? UniString::to_utf8(this->fsPath.wstring()) : this->fsPath.string());
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
    assert(this->ofstream);
    this->ofstream->open(this->fsPath, std::ios::out | std::ios::trunc | std::ios::binary);

    // check if file opened successfully
    if(!this->ofstream->good()) {
        debugLog("File Error: Couldn't open file {:s} (fsPath: {:s}) for writing", this->sFilePath,
                 Env::cfg(OS::WINDOWS) ? UniString::to_utf8(this->fsPath.wstring()) : this->fsPath.string());
        return false;
    }

    return true;
}

bool File::write(std::span<const u8> buffer) {
    logIfCV(debug_file, "{:s} (canWrite: {})", this->sFilePath, canWrite());

    if(!canWrite()) return false;

    return !(this->ofstream->write(reinterpret_cast<const char *>(buffer.data()),
                                   static_cast<std::streamsize>(buffer.size())))
                .bad();
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
