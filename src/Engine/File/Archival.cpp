#include "Archival.h"

#include "Environment.h"
#include "File.h"
#include "Logging.h"
#include "SString.h"
#include "ConVar.h"
#include "Thread.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>

#ifndef _WIN32
#include <sys/stat.h>
#endif

//------------------------------------------------------------------------------
// Archive::Entry implementation
//------------------------------------------------------------------------------

Archive::Entry::Entry(struct archive* archive, struct archive_entry* entry) {
    if(!entry) {
        // create empty entry
        this->sFilename = "";
        this->iUncompressedSize = 0;
        this->iCompressedSize = 0;
        this->iPermissions = 0;
        this->bIsDirectory = false;
        return;
    }

    this->sFilename = archive_entry_pathname(entry);
    this->iUncompressedSize = archive_entry_size(entry);
    this->iCompressedSize = 0;  // libarchive doesn't always provide compressed size
    this->iPermissions = archive_entry_perm(entry);
    this->bIsDirectory = archive_entry_filetype(entry) == AE_IFDIR;

    // extract data immediately while archive is positioned correctly
    if(!this->bIsDirectory && archive) {
        if(this->iUncompressedSize > 0) {
            this->data.reserve(this->iUncompressedSize);
        }

        const void* buff;
        size_t size;
        la_int64_t offset;

        int result;
        while((result = archive_read_data_block(archive, &buff, &size, &offset)) == ARCHIVE_OK) {
            const u8* bytes = static_cast<const u8*>(buff);
            this->data.insert(this->data.end(), bytes, bytes + size);
        }

        // check for errors during data extraction
        if(result != ARCHIVE_EOF) {
            logIfCV(debug_file, "failed to extract data for '{:s}': {:s}", this->sFilename.c_str(),
                    archive_error_string(archive));
            // clear any partial data on error
            this->data.clear();
        }
    }
}

std::string Archive::Entry::getFilename() const { return this->sFilename; }

size_t Archive::Entry::getUncompressedSize() const { return this->iUncompressedSize; }

size_t Archive::Entry::getCompressedSize() const { return this->iCompressedSize; }

bool Archive::Entry::isDirectory() const { return this->bIsDirectory; }

bool Archive::Entry::isFile() const { return !this->bIsDirectory; }

const std::vector<u8>& Archive::Entry::getUncompressedData() const { return this->data; }

bool Archive::Entry::extractToFile(const std::string& outputPath) const {
    if(this->bIsDirectory) return false;

    std::ofstream file(outputPath, std::ios::binary);
    if(!file.good()) {
        logIfCV(debug_file, "failed to create output file {:s}", outputPath.c_str());
        return false;
    }

    if(this->data.empty()) {
        logIfCV(debug_file, "Archive WARNING: extracted data for path {:s} is empty.", outputPath.c_str());
    }

    file.write(reinterpret_cast<const char*>(this->data.data()), static_cast<std::streamsize>(this->data.size()));
    if(file.bad()) {
        logIfCV(debug_file, "failed to write to file {:s}", outputPath.c_str());
        return false;
    }

#ifndef _WIN32
    if(this->iPermissions != 0) {
        chmod(outputPath.c_str(), this->iPermissions);
    }
#endif

    return true;
}

//------------------------------------------------------------------------------
// Archive::Reader implementation
//------------------------------------------------------------------------------

Archive::Reader::Reader(const std::string& filePath) : archive(nullptr), bValid(false), bIterationStarted(false) {
    initFromFile(filePath);
}

Archive::Reader::Reader(const u8* data, size_t size) : archive(nullptr), bValid(false), bIterationStarted(false) {
    initFromMemory(data, size);
}

Archive::Reader::~Reader() { cleanup(); }

void Archive::Reader::initFromFile(const std::string& filePath) {
    this->archive = archive_read_new();
    if(!this->archive) {
        logIfCV(debug_file, "failed to create archive reader");
        return;
    }

    // enable all supported formats and filters
    archive_read_support_format_all(this->archive);
    archive_read_support_filter_all(this->archive);

    int r = archive_read_open_filename(this->archive, filePath.c_str(), 10240);
    if(r != ARCHIVE_OK) {
        logIfCV(debug_file, "failed to open file {:s}: {:s}", filePath.c_str(), archive_error_string(this->archive));
        cleanup();
        return;
    }

    this->bValid = true;
}

void Archive::Reader::initFromMemory(const u8* data, size_t size) {
    if(!data || size == 0) {
        logIfCV(debug_file, "invalid memory buffer");
        return;
    }

    // copy data to our own buffer to ensure it stays alive
    this->vMemoryBuffer.assign(data, data + size);

    this->archive = archive_read_new();
    if(!this->archive) {
        logIfCV(debug_file, "failed to create archive reader");
        return;
    }

    // enable all supported formats and filters
    archive_read_support_format_all(this->archive);
    archive_read_support_filter_all(this->archive);

    int r = archive_read_open_memory(this->archive, this->vMemoryBuffer.data(), this->vMemoryBuffer.size());
    if(r != ARCHIVE_OK) {
        logIfCV(debug_file, "failed to open memory buffer: {:s}", archive_error_string(this->archive));
        cleanup();
        return;
    }

    this->bValid = true;
}

void Archive::Reader::cleanup() {
    this->currentEntry.reset();
    if(this->archive) {
        archive_read_free(this->archive);
        this->archive = nullptr;
    }
    this->bValid = false;
    this->bIterationStarted = false;
}

std::vector<Archive::Entry> Archive::Reader::getAllEntries() {
    std::vector<Entry> entries;
    if(!this->bValid) return entries;

    // restart iteration if needed
    if(this->bIterationStarted) {
        cleanup();
        if(!this->vMemoryBuffer.empty()) {
            initFromMemory(this->vMemoryBuffer.data(), this->vMemoryBuffer.size());
        } else {
            logIfCV(debug_file, "cannot restart iteration on file-based archive");
            return entries;
        }
    }

    struct archive_entry* entry;
    while(archive_read_next_header(this->archive, &entry) == ARCHIVE_OK) {
        entries.emplace_back(this->archive, entry);
        // skip file data to move to next entry
        archive_read_data_skip(this->archive);
    }

    this->bIterationStarted = true;
    return entries;
}

bool Archive::Reader::hasNext() {
    if(!this->bValid) return false;

    if(!this->bIterationStarted) {
        struct archive_entry* entry;
        int r = archive_read_next_header(this->archive, &entry);
        if(r == ARCHIVE_OK) {
            this->currentEntry = std::make_unique<Entry>(this->archive, entry);
            this->bIterationStarted = true;
            return true;
        } else if(r == ARCHIVE_EOF) {
            return false;
        } else {
            logIfCV(debug_file, "error reading next header: {:s}", archive_error_string(this->archive));
            return false;
        }
    }

    return this->currentEntry != nullptr;
}

Archive::Entry Archive::Reader::getCurrentEntry() {
    if(!hasNext()) {
        // return empty entry if none available
        struct archive_entry* dummy = archive_entry_new();
        Entry empty(nullptr, dummy);
        archive_entry_free(dummy);
        return empty;
    }

    return *this->currentEntry;
}

bool Archive::Reader::moveNext() {
    if(!this->bValid) return false;

    // skip current entry data
    if(this->currentEntry) {
        archive_read_data_skip(this->archive);
        this->currentEntry.reset();
    }

    struct archive_entry* entry;
    int r = archive_read_next_header(this->archive, &entry);
    if(r == ARCHIVE_OK) {
        this->currentEntry = std::make_unique<Entry>(this->archive, entry);
        return true;
    } else if(r == ARCHIVE_EOF) {
        return false;
    } else {
        logIfCV(debug_file, "error reading next header: {:s}", archive_error_string(this->archive));
        return false;
    }
}

Archive::Entry* Archive::Reader::findEntry(const std::string& filename) {
    auto entries = getAllEntries();

    auto it =
        std::ranges::find_if(entries, [&filename](const Entry& entry) { return entry.getFilename() == filename; });

    if(it != entries.end()) {
        // note: this returns a pointer to a temporary object
        // caller should use immediately or copy the entry
        static Entry found = *it;
        found = *it;
        return &found;
    }

    return nullptr;
}

bool Archive::Reader::extractAll(const std::string& outputDir, const std::vector<std::string>& ignorePaths,
                                 bool skipDirectories, const Sync::stop_token& stopToken) {
    if(!this->bValid) return false;

    auto entries = getAllEntries();
    if(entries.empty()) return false;

    // separate directories and files
    std::vector<Entry> directories, files;
    for(const auto& entry : entries) {
        if(entry.isDirectory()) {
            directories.push_back(entry);
        } else {
            files.push_back(entry);
        }
    }

    // create directories first (unless skipping)
    if(!skipDirectories) {
        for(const auto& dir : directories) {
            if(stopToken.stop_requested()) {
                logIfCV(debug_file, "extraction interrupted during directory creation");
                return false;
            }

            std::string dirPath = fmt::format("{}/{}", outputDir, dir.getFilename());

            // check ignore list
            bool shouldIgnore = std::ranges::any_of(ignorePaths, [&dirPath](const std::string& ignorePath) {
                return dirPath.find(ignorePath) != std::string::npos;
            });

            if(shouldIgnore) continue;

            if(!isPathSafe(dir.getFilename())) {
                logIfCV(debug_file, "skipping unsafe directory path {:s}", dir.getFilename().c_str());
                continue;
            }

            Environment::createDirectory(dirPath);
        }
    }

    // extract files
    for(const auto& file : files) {
        if(stopToken.stop_requested()) {
            logIfCV(debug_file, "extraction interrupted");
            return false;
        }

        std::string filePath = fmt::format("{}/{}", outputDir, file.getFilename());

        // check ignore list
        bool shouldIgnore = std::ranges::any_of(ignorePaths, [&filePath](const std::string& ignorePath) {
            return filePath.find(ignorePath) != std::string::npos;
        });

        if(shouldIgnore) {
            logIfCV(debug_file, "ignoring file {:s}", filePath.c_str());
            continue;
        }

        if(!isPathSafe(file.getFilename())) {
            logIfCV(debug_file, "skipping unsafe file path {:s}", file.getFilename().c_str());
            continue;
        }

        // ensure parent directory exists
        auto folders = SString::split(file.getFilename(), '/');
        std::string currentPath = outputDir;
        for(size_t i = 0; i < folders.size() - 1; i++) {
            currentPath = fmt::format("{}/{}", currentPath, folders[i]);
            Environment::createDirectory(currentPath);
        }

        if(!file.extractToFile(filePath)) {
            logIfCV(debug_file, "failed to extract file {:s}", filePath.c_str());
            return false;
        }
    }

    return true;
}

bool Archive::Reader::isPathSafe(const std::string& path) { return path.find("..") == std::string::npos; }

//------------------------------------------------------------------------------
// Archive::Writer implementation
//------------------------------------------------------------------------------

Archive::Writer::Writer(Format format, int compressionLevel)
    : compressionLevel(compressionLevel),
      threads(std::clamp<int>(cv::archive_threads.getInt(), 0, McThread::get_logical_cpu_count())),
      format(format) {}

std::string Archive::Writer::normalizePath(const std::string& path) {
    std::string ret = path;
    File::normalizeSlashes(ret, '\\', '/');

    // strip leading slash
    if(!ret.empty() && ret.front() == '/') {
        ret.erase(ret.begin());
    }

    // strip trailing slash for files (directories handled separately)
    while(ret.size() > 1 && ret.back() == '/') {
        ret.pop_back();
    }

    return ret;
}

std::string Archive::Writer::extractFilename(const std::string& path) {
    std::string normalized = path;
    File::normalizeSlashes(normalized, '\\', '/');

    // strip trailing slashes
    while(!normalized.empty() && normalized.back() == '/') {
        normalized.pop_back();
    }

    auto pos = normalized.rfind('/');
    if(pos != std::string::npos) {
        return normalized.substr(pos + 1);
    }
    return normalized;
}

bool Archive::Writer::addFile(const std::string& diskPath, const std::string& archivePath) {
    auto type = File::exists(diskPath);
    if(type == File::FILETYPE::FOLDER) {
        logIfCV(debug_file, "addFile called on directory, use addPath for directories: {:s}", diskPath.c_str());
        return false;
    }

    File file(diskPath, File::MODE::READ);
    if(!file.canRead()) {
        logIfCV(debug_file, "failed to open file for reading: {:s}", diskPath.c_str());
        return false;
    }

    std::vector<u8> data;
    file.readToVector(data);

    std::string finalArchivePath = archivePath;
    if(finalArchivePath.empty()) {
        finalArchivePath = extractFilename(diskPath);
    }

    return addData(finalArchivePath, std::move(data));
}

bool Archive::Writer::addPath(const std::string& diskPath, const std::string& archiveRoot,
                              const Sync::stop_token& stopToken) {
    auto type = File::exists(diskPath);

    if(type == File::FILETYPE::FILE) {
        std::string filename = extractFilename(diskPath);
        std::string archivePath = archiveRoot.empty() ? filename : fmt::format("{}/{}", archiveRoot, filename);
        return addFile(diskPath, archivePath);
    } else if(type == File::FILETYPE::FOLDER) {
        return addDirectoryRecursive(diskPath, archiveRoot, stopToken);
    }

    logIfCV(debug_file, "path does not exist or is not accessible: {:s}", diskPath.c_str());
    return false;
}

bool Archive::Writer::addDirectoryRecursive(const std::string& diskDir, const std::string& archiveDir,
                                            const Sync::stop_token& stopToken) {
    if(stopToken.stop_requested()) {
        logIfCV(debug_file, "addDirectoryRecursive interrupted");
        return false;
    }

    // add files in this directory
    std::vector<std::string> files;
    if(!File::getDirectoryEntries(diskDir, File::DirContents::FILES, files)) {
        logIfCV(debug_file, "failed to enumerate files in {:s}", diskDir.c_str());
        return false;
    }

    for(const auto& filename : files) {
        if(stopToken.stop_requested()) {
            logIfCV(debug_file, "addDirectoryRecursive interrupted during file enumeration");
            return false;
        }

        std::string diskPath = fmt::format("{}/{}", diskDir, filename);
        std::string archivePath = archiveDir.empty() ? filename : fmt::format("{}/{}", archiveDir, filename);

        if(!addFile(diskPath, archivePath)) {
            logIfCV(debug_file, "failed to add file: {:s}", diskPath.c_str());
            return false;
        }
    }

    // recurse into subdirectories
    std::vector<std::string> dirs;
    if(!File::getDirectoryEntries(diskDir, File::DirContents::DIRECTORIES, dirs)) {
        logIfCV(debug_file, "failed to enumerate directories in {:s}", diskDir.c_str());
        return false;
    }

    for(const auto& dirname : dirs) {
        std::string subDiskDir = fmt::format("{}/{}", diskDir, dirname);
        std::string subArchiveDir = archiveDir.empty() ? dirname : fmt::format("{}/{}", archiveDir, dirname);

        if(!addDirectoryRecursive(subDiskDir, subArchiveDir, stopToken)) {
            return false;
        }
    }

    return true;
}

bool Archive::Writer::addData(const std::string& archivePath, const u8* data, size_t size) {
    if(archivePath.empty()) {
        logIfCV(debug_file, "empty archive path");
        return false;
    }

    std::string normalizedPath = normalizePath(archivePath);
    if(normalizedPath.empty()) {
        logIfCV(debug_file, "path normalized to empty string");
        return false;
    }

    PendingEntry entry;
    entry.archivePath = std::move(normalizedPath);
    entry.data.assign(data, data + size);
    entry.isDirectory = false;

    this->pendingEntries.push_back(std::move(entry));
    return true;
}

bool Archive::Writer::addData(const std::string& archivePath, const std::vector<u8>& data) {
    return addData(archivePath, data.data(), data.size());
}

bool Archive::Writer::addData(const std::string& archivePath, std::vector<u8>&& data) {
    if(archivePath.empty()) {
        logIfCV(debug_file, "empty archive path");
        return false;
    }

    std::string normalizedPath = normalizePath(archivePath);
    if(normalizedPath.empty()) {
        logIfCV(debug_file, "path normalized to empty string");
        return false;
    }

    PendingEntry entry;
    entry.archivePath = std::move(normalizedPath);
    entry.data = std::move(data);
    entry.isDirectory = false;

    this->pendingEntries.push_back(std::move(entry));
    return true;
}

bool Archive::Writer::configureArchive(struct archive* a) {
    int r;
    std::string options;

    switch(this->format) {
        case Format::ZIP:
            r = archive_write_set_format_zip(a);
            if(r != ARCHIVE_OK) {
                logIfCV(debug_file, "failed to set ZIP format: {:s}", archive_error_string(a));
                return false;
            }

            if(this->compressionLevel == COMPRESSION_STORE) {
                options += "zip:compression=store,";
            } else if(this->compressionLevel != COMPRESSION_DEFAULT) {
                options += fmt::format("zip:compression=deflate,zip:compression-level={:d},",
                                       std::clamp(this->compressionLevel, 1, 9));
            }
            if(this->threads != 1) {
                options += fmt::format("zip:threads={:d},", this->threads);
            }
            break;

        case Format::SEVENZIP_DEFLATE:
        case Format::SEVENZIP_BZ2:
        case Format::SEVENZIP_LZMA1:
        case Format::SEVENZIP_LZMA2:
            r = archive_write_set_format_7zip(a);
            if(r != ARCHIVE_OK) {
                logIfCV(debug_file, "failed to set SEVENZIP format: {:s}", archive_error_string(a));
                return false;
            }

            {
                const std::string_view compression = this->compressionLevel == COMPRESSION_STORE ? "store"
                                                     : this->format == Format::SEVENZIP_BZ2      ? "bzip2"
                                                     : this->format == Format::SEVENZIP_LZMA1    ? "lzma1"
                                                     : this->format == Format::SEVENZIP_LZMA2    ? "lzma2"
                                                                                                 : "deflate";
                options += fmt::format("7zip:compression={:s},", compression);
            }
            if(this->compressionLevel != COMPRESSION_DEFAULT) {
                int level = std::clamp(this->compressionLevel, this->format == Format::SEVENZIP_BZ2 ? 1 : 0, 9);
                options += fmt::format("7zip:compression-level={:d},", level);
            }
            if(this->threads != 1) {
                options += fmt::format("7zip:threads={:d},", this->threads);
            }
            break;

        case Format::TAR:
            r = archive_write_set_format_pax_restricted(a);
            if(r != ARCHIVE_OK) {
                logIfCV(debug_file, "failed to set TAR format: {:s}", archive_error_string(a));
                return false;
            }
            break;

        case Format::TAR_GZ:
            r = archive_write_set_format_pax_restricted(a);
            if(r != ARCHIVE_OK) {
                logIfCV(debug_file, "failed to set TAR format: {:s}", archive_error_string(a));
                return false;
            }
            r = archive_write_add_filter_gzip(a);
            if(r != ARCHIVE_OK) {
                logIfCV(debug_file, "failed to add gzip filter: {:s}", archive_error_string(a));
                return false;
            }
            if(this->compressionLevel != COMPRESSION_DEFAULT && this->compressionLevel != COMPRESSION_STORE) {
                options += fmt::format("gzip:compression-level={:d},", std::clamp(this->compressionLevel, 1, 9));
            }
            break;

        case Format::TAR_BZ2:
            r = archive_write_set_format_pax_restricted(a);
            if(r != ARCHIVE_OK) {
                logIfCV(debug_file, "failed to set TAR format: {:s}", archive_error_string(a));
                return false;
            }
            r = archive_write_add_filter_bzip2(a);
            if(r != ARCHIVE_OK) {
                logIfCV(debug_file, "failed to add bzip2 filter: {:s}", archive_error_string(a));
                return false;
            }
            if(this->compressionLevel != COMPRESSION_DEFAULT && this->compressionLevel != COMPRESSION_STORE) {
                options += fmt::format("bzip2:compression-level={:d},", std::clamp(this->compressionLevel, 1, 9));
            }
            break;

        case Format::TAR_XZ:
            r = archive_write_set_format_pax_restricted(a);
            if(r != ARCHIVE_OK) {
                logIfCV(debug_file, "failed to set TAR format: {:s}", archive_error_string(a));
                return false;
            }
            r = archive_write_add_filter_xz(a);
            if(r != ARCHIVE_OK) {
                logIfCV(debug_file, "failed to add xz filter: {:s}", archive_error_string(a));
                return false;
            }
            if(this->compressionLevel != COMPRESSION_DEFAULT && this->compressionLevel != COMPRESSION_STORE) {
                options += fmt::format("xz:compression-level={:d},", std::clamp(this->compressionLevel, 0, 9));
            }
            if(this->threads != 1) {
                options += fmt::format("xz:threads={:d},", this->threads);
            }
            break;
    }

    if(!options.empty()) {
        if(options.ends_with(',')) {
            options.pop_back();
        }
        archive_write_set_options(a, options.c_str());
    }

    return true;
}

bool Archive::Writer::writeEntries(struct archive* a, const Sync::stop_token& stopToken) {
    time_t now = std::time(nullptr);
    // libarchive has no native way to asynchronously cancel an operation, so write in chunks
    // so that we have a chance to do so
    constexpr size_t CHUNK_SIZE = 64ULL * 1024;

    for(const auto& pending : this->pendingEntries) {
        if(stopToken.stop_requested()) {
            logIfCV(debug_file, "write interrupted before entry '{:s}'", pending.archivePath.c_str());
            archive_write_fail(a);
            return false;
        }

        struct archive_entry* entry = archive_entry_new();
        if(!entry) {
            logIfCV(debug_file, "failed to create archive entry");
            return false;
        }

        archive_entry_set_pathname(entry, pending.archivePath.c_str());
        archive_entry_set_mtime(entry, now, 0);

        if(pending.isDirectory) {
            archive_entry_set_filetype(entry, AE_IFDIR);
            archive_entry_set_perm(entry, 0755);
            archive_entry_set_size(entry, 0);
        } else {
            archive_entry_set_filetype(entry, AE_IFREG);
            archive_entry_set_perm(entry, 0644);
            archive_entry_set_size(entry, static_cast<la_int64_t>(pending.data.size()));
        }

        int r = archive_write_header(a, entry);
        if(r != ARCHIVE_OK) {
            logIfCV(debug_file, "failed to write header for '{:s}': {:s}", pending.archivePath.c_str(),
                    archive_error_string(a));
            archive_entry_free(entry);
            return false;
        }

        if(!pending.isDirectory && !pending.data.empty()) {
            const u8* ptr = pending.data.data();
            size_t remaining = pending.data.size();

            while(remaining > 0) {
                if(stopToken.stop_requested()) {
                    logIfCV(debug_file, "write interrupted during '{:s}'", pending.archivePath.c_str());
                    archive_entry_free(entry);
                    archive_write_fail(a);
                    return false;
                }

                size_t toWrite = std::min(remaining, CHUNK_SIZE);
                la_ssize_t written = archive_write_data(a, ptr, toWrite);
                if(written < 0) {
                    logIfCV(debug_file, "failed to write data for '{:s}': {:s}", pending.archivePath.c_str(),
                            archive_error_string(a));
                    archive_entry_free(entry);
                    return false;
                }
                ptr += written;
                remaining -= static_cast<size_t>(written);
            }
        }

        archive_entry_free(entry);
    }

    return true;
}

bool Archive::Writer::writeToFile(std::string outputPath, bool appendExtension, const Sync::stop_token& stopToken) {
    if(this->pendingEntries.empty()) {
        logIfCV(debug_file, "no entries to write");
        return false;
    }

    struct archive* a = archive_write_new();
    if(!a) {
        logIfCV(debug_file, "failed to create archive writer");
        return false;
    }

    if(!configureArchive(a)) {
        archive_write_free(a);
        return false;
    }

    if(appendExtension) {
        outputPath += getExtSuffix();
    }

    int r = archive_write_open_filename(a, outputPath.c_str());
    if(r != ARCHIVE_OK) {
        logIfCV(debug_file, "error opening: {:s}", archive_error_string(a));
        archive_write_free(a);
        return false;
    }

    bool success = writeEntries(a, stopToken);

    r = archive_write_close(a);
    if(r != ARCHIVE_OK) {
        logIfCV(debug_file, "error closing: {:s}", archive_error_string(a));
        success = false;
    }

    archive_write_free(a);
    return success;
}

std::vector<u8> Archive::Writer::writeToMemory(const Sync::stop_token& stopToken) {
    std::vector<u8> result;

    if(this->pendingEntries.empty()) {
        logIfCV(debug_file, "no entries to write");
        return result;
    }

    struct archive* a = archive_write_new();
    if(!a) {
        logIfCV(debug_file, "failed to create archive writer");
        return result;
    }

    if(!configureArchive(a)) {
        archive_write_free(a);
        return result;
    }

    struct WriteContext {
        std::vector<u8>* output;
        const Sync::stop_token* stopToken;
    };
    WriteContext ctx{&result, &stopToken};

    auto writeCallback = [](struct archive*, void* clientData, const void* buffer, size_t length) -> la_ssize_t {
        auto* c = static_cast<WriteContext*>(clientData);
        if(c->stopToken->stop_requested()) {
            return ARCHIVE_FATAL;
        }
        const u8* bytes = static_cast<const u8*>(buffer);
        c->output->insert(c->output->end(), bytes, bytes + length);
        return static_cast<la_ssize_t>(length);
    };

    int r = archive_write_open(a, &ctx, nullptr, writeCallback, nullptr);
    if(r != ARCHIVE_OK) {
        logIfCV(debug_file, "failed to open memory output: {:s}", archive_error_string(a));
        archive_write_free(a);
        return {};
    }

    if(!writeEntries(a, stopToken)) {
        archive_write_free(a);
        return {};
    }

    r = archive_write_close(a);
    if(r != ARCHIVE_OK) {
        logIfCV(debug_file, "failed to close archive: {:s}", archive_error_string(a));
        archive_write_free(a);
        return {};
    }

    archive_write_free(a);
    return result;
}
