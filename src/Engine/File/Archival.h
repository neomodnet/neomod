#pragma once

#include "noinclude.h"
#include "types.h"
#include "SyncStoptoken.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <span>

struct archive;
struct archive_entry;

class Archive {
   public:
    enum class Format : uint8_t {
        ZIP,
        SEVENZIP_DEFLATE,
        SEVENZIP_BZ2,
        SEVENZIP_LZMA1,
        SEVENZIP_LZMA2,
        TAR,
        TAR_GZ,
        TAR_BZ2,
        TAR_XZ,
    };

    static constexpr std::string_view getExtSuffix(Format fmt) {
        switch(fmt) {
            case Format::ZIP:
            case Format::SEVENZIP_DEFLATE:
                return ".zip";
            case Format::SEVENZIP_BZ2:
            case Format::SEVENZIP_LZMA1:
            case Format::SEVENZIP_LZMA2:
                return ".7z";
            case Format::TAR:
                return ".tar";
            case Format::TAR_GZ:
                return ".tar.gz";
            case Format::TAR_BZ2:
                return ".tar.bz2";
            case Format::TAR_XZ:
                return ".tar.xz";
        }
        std::unreachable();
    }

    static constexpr int COMPRESSION_STORE = 0;
    static constexpr int COMPRESSION_DEFAULT = -1;
    static constexpr int COMPRESSION_MAX = 9;

    class Entry {
       public:
        Entry(struct archive* archive, struct archive_entry* entry);
        ~Entry() = default;

        Entry& operator=(const Entry&) = default;
        Entry& operator=(Entry&&) noexcept = default;
        Entry(const Entry&) = default;
        Entry(Entry&&) noexcept = default;

        // entry information
        [[nodiscard]] const std::string& getFilename() const;
        [[nodiscard]] size_t getUncompressedSize() const;
        [[nodiscard]] size_t getCompressedSize() const;
        [[nodiscard]] bool isDirectory() const;
        [[nodiscard]] bool isFile() const;

        // extraction methods
        [[nodiscard]] const std::vector<u8>& getUncompressedData() const;
        [[nodiscard]] bool extractToFile(std::string_view outputPath) const;

       private:
        std::string sFilename;
        size_t iUncompressedSize;
        size_t iCompressedSize;
        unsigned iPermissions;
        bool bIsDirectory;
        std::vector<u8> data;  // store extracted data
    };

    class Reader {
        NOCOPY_NOMOVE(Reader)
       public:
        // construct from file path; hdrCharset sets libarchive's assumed charset for non-UTF8-flagged
        // entry names (e.g. "CP932" for Shift-JIS)
        explicit Reader(const std::string& filePath, std::string_view hdrCharset = {});

        // construct from memory buffer
        Reader(std::span<const u8> data, std::string_view hdrCharset = {});
        ~Reader();

        // check if archive was opened successfully
        [[nodiscard]] bool isValid() const { return this->bValid; }

        // get all entries at once (useful for separating files/dirs)
        std::vector<Entry> getAllEntries();

        // iteration interface
        bool hasNext();
        Entry getCurrentEntry();
        bool moveNext();

        // convenience methods
        Entry* findEntry(std::string_view filename);
        bool extractAll(std::string_view outputDir, const std::vector<std::string>& ignorePaths = {},
                        bool skipDirectories = false, const Sync::stop_token& stopToken = {});

       private:
        void initFromFile(const std::string& filePath);
        void initFromMemory(std::span<const u8> data);
        void cleanup();
        [[nodiscard]] static bool isPathSafe(std::string_view path);

        struct archive* archive;
        std::vector<u8> vMemoryBuffer;
        bool bValid;
        bool bIterationStarted;
        std::unique_ptr<Entry> currentEntry;
        std::string sHdrCharset;  // assumed charset for non-UTF8-flagged entry names; empty = libarchive default
    };

    class Writer {
        NOCOPY_NOMOVE(Writer)
       public:
        explicit Writer(std::string_view hdrCharset);
        explicit Writer(Format format = Format::ZIP, int compressionLevel = COMPRESSION_DEFAULT,
                        std::string_view hdrCharset = {});
        ~Writer() = default;

        // add a single file from disk; fails if diskPath is a directory
        // if archivePath is empty, uses filename from diskPath
        bool addFile(std::string_view diskPath, std::string_view archivePath = "");

        // add file or directory recursively from disk
        // if diskPath is a file, adds it at archiveRoot/filename
        // if diskPath is a directory, adds all contents recursively under archiveRoot
        bool addPath(std::string_view diskPath, std::string_view archiveRoot = "",
                     const Sync::stop_token& stopToken = {});

        // add file from memory
        bool addData(std::string_view archivePath, std::span<const u8> data);
        bool addData(std::string_view archivePath, std::vector<u8>&& data);

        bool writeToFile(std::string outputPath, bool appendExtension = true, const Sync::stop_token& stopToken = {});
        [[nodiscard]] std::vector<u8> writeToMemory(const Sync::stop_token& stopToken = {});

        [[nodiscard]] bool isEmpty() const { return this->pendingEntries.empty(); }
        [[nodiscard]] size_t getEntryCount() const { return this->pendingEntries.size(); }
        [[nodiscard]] constexpr std::string_view getExtSuffix() const { return Archive::getExtSuffix(this->format); }

       private:
        struct PendingEntry {
            std::string archivePath;
            std::vector<u8> data;
            bool isDirectory;
        };

        bool configureArchive(struct archive* a);
        bool writeEntries(struct archive* a, const Sync::stop_token& stopToken);
        bool addDirectoryRecursive(std::string_view diskDir, std::string_view archiveDir,
                                   const Sync::stop_token& stopToken);

        [[nodiscard]] static std::string normalizePath(std::string_view path);
        [[nodiscard]] static std::string extractFilename(std::string_view path);

        std::vector<PendingEntry> pendingEntries;
        int compressionLevel;
        int threads;
        Format format;
        std::string sHdrCharset;  // empty = libarchive default
    };
};
