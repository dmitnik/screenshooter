#pragma once

#include "capture/pixel_buffer.h"

#include <windows.h>

#include <memory>

namespace screenshooter::exporting {

class ShareService final {
public:
    ShareService();
    ~ShareService();
    ShareService(const ShareService&) = delete;
    ShareService& operator=(const ShareService&) = delete;

    [[nodiscard]] bool Show(HWND owner, const capture::PixelBuffer& image);
    static void CleanupOldFiles();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace screenshooter::exporting
