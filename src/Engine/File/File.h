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

#include "noinclude.h"
#include "types.h"
#include "UString.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#include <sys/stat.h>

#ifdef MCENGINE_PLATFORM_WINDOWS
#define stat64 __stat64
#endif
#ifdef __APPLE__
#define stat64 stat
#endif

class ConVar;

class File {
    NOCOPY_NOMOVE(File)
   public:
    enum class MODE : uint8_t { READ, WRITE };

    enum class FILETYPE : uint8_t { NONE, FILE, FOLDER, MAYBE_INSENSITIVE, OTHER };

   public:
    File(std::string_view filePath, MODE mode = MODE::READ);
    ~File() = default;

    [[nodiscard]] constexpr bool canRead() const {
        return this->bReady && this->ifstream && this->ifstream->good() && this->fileMode == MODE::READ;
    }
    [[nodiscard]] constexpr bool canWrite() const {
        return this->bReady && this->ofstream && this->ofstream->good() && this->fileMode == MODE::WRITE;
    }

    void write(const u8 *buffer, uSz size);
    bool writeLine(std::string_view line, bool insertNewline = true);

    std::string readLine();
    std::string readToString();

    // returns actual amount read
    uSz readBytes(uSz start, uSz amount, std::unique_ptr<u8[]> &out);

    // WARNING: this is NOT a null-terminated string! DO NOT USE THIS with UString/std::string!
    const std::unique_ptr<u8[]> &readFile();

    // moves the file buffer out, allowing immediate destruction of the file object
    [[nodiscard]] std::unique_ptr<u8[]> &&takeFileBuffer();

    void readToVector(std::vector<u8> &out);

    [[nodiscard]] inline std::string_view getPath() const { return this->sFilePath.utf8View(); }

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
    static bool getDirectoryEntries(const std::string &toEnumerate, DirContents types,
                                    std::vector<std::string> &utf8NamesOut) noexcept;

    // fs::path works differently depending on the type of string it was constructed with
    // so use this to get a unicode-constructed path on windows (convert), utf8 otherwise (passthrough)
    [[nodiscard]] static std::filesystem::path getFsPath(std::string_view utf8path);

    // passthrough to "_wfopen" on Windows, "fopen" otherwise
    [[nodiscard]] static FILE *fopen_c(const char *__restrict utf8filename, const char *__restrict modes);

    // passthrough to "_wstat64" on Windows, "stat64" otherwise
    [[nodiscard]] static int stat_c(const char *__restrict utf8filename, struct stat64 *__restrict buffer);

    // copy file from source to destination, overwriting if exists
    [[nodiscard]] static bool copy(std::string_view fromPath, std::string_view toPath);

    // save indexedDB for wasm, noop on other platforms
    static void flushToDisk();

   private:
    // private implementation helpers
    bool openForReading();
    bool openForWriting();

    // internal path resolution helpers
    [[nodiscard]] static File::FILETYPE existsCaseInsensitive(std::string &filePath, std::filesystem::path &path);
    [[nodiscard]] static File::FILETYPE exists(std::string_view filePath, const std::filesystem::path &path);

    UString sFilePath;
    std::filesystem::path fsPath;

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

#endif
