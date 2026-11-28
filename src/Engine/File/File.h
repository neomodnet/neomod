//========== Copyright (c) 2016, PG & 2025, WH, All rights reserved. ============//
//
// Purpose:		file wrapper, for cross-platform unicode path support
//
// $NoKeywords: $file
//===============================================================================//

#pragma once
#ifndef FILE_H
#define FILE_H
#include "config.h"

// MSVC things
#ifdef _CRT_INTERNAL_NONSTDC_NAMES
#undef _CRT_INTERNAL_NONSTDC_NAMES
#endif
#define _CRT_INTERNAL_NONSTDC_NAMES 1

#include "noinclude.h"
#include "types.h"

#include "CStrView.h"

#include <iosfwd>
#include <memory>
#include <string>
#include <vector>
#include <span>

#include <sys/stat.h>

#ifdef MCENGINE_PLATFORM_WINDOWS
#define stat64 __stat64
#endif

#ifdef MCENGINE_PLATFORM_MACOS
#ifndef stat64
#define stat64 stat
#endif
#ifndef openat64
#define openat64 openat
#endif
#ifndef readdir64
#define readdir64 readdir
#endif
#ifndef dirent64
#define dirent64 dirent
#endif
#ifndef fstatat64
#define fstatat64 fstatat
#endif
#ifndef fstat64
#define fstat64 fstat
#endif
#endif

class ConVar;

class File {
    NOCOPY_NOMOVE(File)
   public:
    enum class MODE : uint8_t { READ, WRITE };

    enum class FILETYPE : uint8_t { NONE, FILE, FOLDER, MAYBE_INSENSITIVE, OTHER };

   public:
    File(std::string_view filePath, MODE mode = MODE::READ);
    ~File();

    [[nodiscard]] bool canRead() const;
    [[nodiscard]] bool canWrite() const;

    bool write(std::span<const u8> buffer);
    bool writeLine(std::string_view line, bool insertNewline = true);

    // commit the writes. returns false if write failed.
    bool commit();

    std::string readLine();
    std::string readToString();

    // returns actual amount read
    uSz readBytes(uSz start, uSz amount, std::unique_ptr<u8[]> &out);

    // WARNING: this is NOT a null-terminated string! DO NOT USE THIS with UString/std::string!
    const std::unique_ptr<u8[]> &readFile();

    // moves the file buffer out, allowing immediate destruction of the file object
    [[nodiscard]] std::unique_ptr<u8[]> &&takeFileBuffer();

    void readToVector(std::vector<u8> &out);

    [[nodiscard]] inline const std::string &getPath() const { return this->sFilePath; }

    // in bytes
    [[nodiscard]] forceinline uSz getFileSize() const { return this->fsstat.st_size; }
    // unix timestamp in seconds (64-bit time_t)
    [[nodiscard]] forceinline i64 getModificationTime() const { return this->fsstat.st_mtime; }

    // static utils below
    // should be self-explanatory, doesn't actually touch anything on the filesystem, but it's done often enough that it can come in handy
    static void normalizeSlashes(std::string &str, unsigned char removeSlash = '\\',
                                 unsigned char replacementSlash = '/') noexcept;

    // public path resolution methods
    // modifies the input path with the actual found path
    [[nodiscard]] static File::FILETYPE existsCaseInsensitive(std::string &filePath);
    [[nodiscard]] static File::FILETYPE exists(std::string_view filePath);

    // only returns true if succeeded, appends to the input vector
    enum class DirContents : u8 { DIRECTORIES = 1 << 0, FILES = 1 << 1, ALL = DIRECTORIES | FILES };
    static bool getDirectoryEntries(std::string_view toEnumerate, DirContents types,
                                    std::vector<std::string> &utf8NamesOut) noexcept;

    // same, with what the listing itself knows about each entry, so callers don't have to stat every entry by path
    // again. symlinks count as what they point at. the type is always free; withMetadata fills in mtime/size, which
    // is free on windows, one syscall per few hundred entries on macos, and one fstatat per entry elsewhere
    struct DirEntry {
        std::string name;
        FILETYPE type{FILETYPE::NONE};  // FOLDER, FILE, or OTHER (fifos, sockets, ...; those are listed under FILES)
        i64 mtime{0};                   // unix timestamp in seconds, like getModificationTime()
        u32 mtime_nsec{0};              // its sub-second part (0 on filesystems without one)
        u64 size{0};                    // in bytes (files only)
    };
    static bool getDirectoryEntries(std::string_view toEnumerate, DirContents types, std::vector<DirEntry> &entriesOut,
                                    bool withMetadata = true) noexcept;

    // for cache directories: of the files in dir that managed() accepts (the rest are left alone), deletes the ones last
    // modified more than maxAgeSeconds ago and then, counting from the newest, the ones past maxTotalBytes; returns the
    // rest, newest first. lists and deletes on the calling thread, so run it off the main thread
    static std::vector<DirEntry> pruneDirectory(std::string_view dir, bool (*managed)(std::string_view name),
                                                i64 maxAgeSeconds, u64 maxTotalBytes = UINT64_MAX);

    static bool createDirectoryRecursive(std::string_view dir);

    // passthrough to "_wfopen" on Windows, "fopen" otherwise
    [[nodiscard]] static FILE *fopen_c(Mc::cstr_view utf8filename, const char *modes) noexcept;

    // stat64, via the win32 file attributes on Windows (mode/size/times only; the timestamps convert straight from
    // utc there, matching what getDirectoryEntries reports)
    [[nodiscard]] static int stat_c(Mc::cstr_view utf8filename, struct stat64 *buffer) noexcept;

    // copy file from source to destination, overwriting if exists
    [[nodiscard]] static bool copy(std::string_view fromPath, std::string_view toPath);

    // writers write to tmpPathFor(path), then commitTmp() replaces path with it, or deletes it if !written.
    // returns whether path now holds the new file
    [[nodiscard]] static std::string tmpPathFor(std::string_view path);
    static bool commitTmp(std::string_view path, bool written);

    // save indexedDB for wasm, noop on other platforms
    static void flushToDisk();

   private:
    // private implementation helpers
    bool openForReading();
    bool openForWriting();

    std::string sFilePath;

    // file streams
    std::unique_ptr<std::ifstream> ifstream;
    std::unique_ptr<std::ofstream> ofstream;

    // buffer for full file reading
    std::unique_ptr<u8[]> vFullBuffer;

    struct stat64 fsstat{};

    MODE fileMode;
    bool bReady;
};
MAKE_FLAG_ENUM(File::DirContents)

// a whole file mapped read-only into memory; the os pages it in as it's read, so large files cost little until used.
// for handing files to libraries that read from a buffer instead of opening (unicode) paths themselves
class MappedFile final {
    NOCOPY_NOMOVE(MappedFile)
   public:
    explicit MappedFile(Mc::cstr_view utf8path);
    ~MappedFile();

    // empty if the file couldn't be opened or mapped
    [[nodiscard]] inline std::span<const u8> data() const { return this->view; }

   private:
    std::span<const u8> view;
};

#endif
