#pragma once

#include "capture/pixel_buffer.h"

#include <windows.h>

#include <optional>

namespace screenshooter::exporting {

class ClipboardService final {
public:
    [[nodiscard]] static bool CopyImage(
        HWND owner,
        const capture::PixelBuffer& image
    );
    [[nodiscard]] static std::optional<capture::PixelBuffer> ReadImage(HWND owner);
};

}  // namespace screenshooter::exporting
