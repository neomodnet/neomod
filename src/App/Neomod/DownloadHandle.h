#pragma once

#include "types.h"

#include <memory>
#include <vector>

namespace Downloader {

struct Request;

// RAII handle for download lifetime management.
// When all handles for a download drop, the queue entry becomes eligible for cleanup.
class DownloadHandle : public std::shared_ptr<Request> {
   public:
    using shared_ptr::shared_ptr;

    DownloadHandle(std::shared_ptr<Request> &&request) : shared_ptr(std::move(request)) {}

    [[nodiscard]] float progress() const;     // 0.0 when nullptr, 0.0-1.0 during download
    [[nodiscard]] int response_code() const;  // 0 when nullptr or not completed
    [[nodiscard]] bool completed() const;     // false when nullptr
    [[nodiscard]] bool failed() const;        // true when !nullptr && progress < 0

    // Move downloaded bytes out. Empty if !completed() or failed().
    [[nodiscard]] std::vector<u8> take_data();
};

}  // namespace Downloader
